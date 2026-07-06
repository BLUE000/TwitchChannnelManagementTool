#include "PluginLoader.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "logger/Logger.h"
#include "SignalDispatcher.h"

PluginLoader::PluginLoader(QObject* parent) : QObject(parent) {}

PluginLoader::~PluginLoader() {
    // 現在ロードされているプラグインをすべて安全にアンロード
    QStringList paths = m_loadedPlugins.keys();
    for (const QString& path : paths) {
        unloadPlugin(path);
    }
}

QList<QString> PluginLoader::scanPlugins() {
    QList<QString> list;
    QString appDir = QCoreApplication::applicationDirPath();
    QString pluginsPath = QDir(appDir).filePath("plugins");
    
    QDir dir(pluginsPath);
    if (!dir.exists()) {
        dir.mkpath(".");
    }
    
    QStringList filters;
#ifdef Q_OS_WIN
    filters << "*.dll";
#elif defined(Q_OS_MAC)
    filters << "*.dylib";
#else
    filters << "*.so";
#endif
    
    QFileInfoList infoList = dir.entryInfoList(filters, QDir::Files);
    for (const QFileInfo& info : infoList) {
        list.append(info.absoluteFilePath());
    }
    return list;
}

IChannelPlugin* PluginLoader::loadPlugin(const QString& filePath, ICoreContext* context, QWidget* uiParent) {
    if (m_loadedPlugins.contains(filePath)) {
        return m_loadedPlugins[filePath].instance;
    }
    
    QPluginLoader* loader = new QPluginLoader(filePath, this);
    QObject* instance = loader->instance();
    if (!instance) {
        Logger::instance().log("ERROR", "PluginLoader", "loadPlugin", 
                               QString("Failed to load DLL %1: %2").arg(filePath).arg(loader->errorString()));
        delete loader;
        return nullptr;
    }
    
    IChannelPlugin* plugin = qobject_cast<IChannelPlugin*>(instance);
    if (!plugin) {
        Logger::instance().log("ERROR", "PluginLoader", "loadPlugin", 
                               QString("Cast failed for DLL %1: Does not implement IChannelPlugin").arg(filePath));
        loader->unload();
        delete loader;
        return nullptr;
    }
    
    // デフォルトアセットの自動展開
    extractDefaultAssets(plugin);
    
    // 初期化
    plugin->initialize(context);
    
    // GUIウィジェットの生成
    QWidget* widget = plugin->createWidget(uiParent);
    
    // シグナル・スロット接続 (Lambdaを使用して pure virtual インターフェースへ接続)
    QList<QMetaObject::Connection> connections;
    SignalDispatcher& dispatcher = SignalDispatcher::instance();
    
    connections.append(connect(&dispatcher, &SignalDispatcher::commentReceived, this, [plugin](const TwitchComment& comment) {
        plugin->onCommentReceived(comment);
    }));
    
    connections.append(connect(&dispatcher, &SignalDispatcher::rewardRedeemed, this, [plugin](const TwitchRewardRedemption& redemption) {
        plugin->onRewardRedeemed(redemption);
    }));
    
    connections.append(connect(&dispatcher, &SignalDispatcher::tick, this, [plugin]() {
        plugin->onTick();
    }));
    
    LoadedPlugin lp;
    lp.filePath = filePath;
    lp.loader = loader;
    lp.instance = plugin;
    lp.widget = widget;
    lp.connections = connections;
    
    m_loadedPlugins[filePath] = lp;
    
    Logger::instance().log("INFO", "PluginLoader", "loadPlugin", 
                           QString("Plugin loaded successfully: %1").arg(plugin->pluginName()));
    return plugin;
}

bool PluginLoader::unloadPlugin(const QString& filePath) {
    if (!m_loadedPlugins.contains(filePath)) {
        return false;
    }
    
    LoadedPlugin lp = m_loadedPlugins[filePath];
    
    // 1. シグナル接続解除
    for (const QMetaObject::Connection& conn : lp.connections) {
        disconnect(conn);
    }
    
    // 2. UIの安全破棄 (DLLのアンロード前に確実に delete しメモリクラッシュを防ぐ)
    if (lp.widget) {
        delete lp.widget;
        lp.widget = nullptr;
    }
    
    // 3. shutdown の実行
    if (lp.instance) {
        lp.instance->shutdown();
    }
    
    // 4. DLLのアンロード
    bool ok = lp.loader->unload();
    if (ok) {
        Logger::instance().log("INFO", "PluginLoader", "unloadPlugin", 
                               QString("Plugin unloaded successfully: %1").arg(filePath));
    } else {
        Logger::instance().log("ERROR", "PluginLoader", "unloadPlugin", 
                               QString("Failed to unload plugin %1: %2").arg(filePath).arg(lp.loader->errorString()));
    }
    
    delete lp.loader;
    m_loadedPlugins.remove(filePath);
    return ok;
}

QList<LoadedPlugin> PluginLoader::loadedPlugins() const {
    return m_loadedPlugins.values();
}

void PluginLoader::extractDefaultAssets(IChannelPlugin* plugin) {
    QMap<QString, QByteArray> assets = plugin->defaultAssets();
    QString appDir = QCoreApplication::applicationDirPath();
    
    for (auto it = assets.begin(); it != assets.end(); ++it) {
        QString relativePath = it.key();
        QByteArray data = it.value();
        
        if (relativePath.startsWith("/")) {
            relativePath = relativePath.mid(1);
        }
        
        QString outPath = QDir(appDir).filePath(
            QString("assets/overlay/%1/default/%2")
            .arg(plugin->pluginName())
            .arg(relativePath)
        );
        
        QFile file(outPath);
        if (file.exists()) {
            // 上書き制限ルール (ユーザーが直接編集したアセットの保護)
            Logger::instance().log("INFO", "PluginLoader", "extractDefaultAssets", 
                                   QString("Asset already exists, skipping: %1").arg(outPath));
            continue;
        }
        
        // フォルダの再帰作成
        QDir().mkpath(QFileInfo(outPath).absolutePath());
        
        if (file.open(QIODevice::WriteOnly)) {
            file.write(data);
            file.close();
            Logger::instance().log("INFO", "PluginLoader", "extractDefaultAssets", 
                                   QString("Extracted default asset to: %1").arg(outPath));
        } else {
            Logger::instance().log("ERROR", "PluginLoader", "extractDefaultAssets", 
                                   QString("Failed to write default asset: %1").arg(outPath));
        }
    }
}
