#include "PluginLoader.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include "logger/Logger.h"
#include "SignalDispatcher.h"
#include <QMutex>
#include <QMutexLocker>
#include <QJsonObject>
#include "cipher_engine.h"

PluginContext::PluginContext(ICoreContext* baseContext, const QString& pluginId)
    : m_base(baseContext), m_pluginId(pluginId) {}

void PluginContext::sendChatMessage(const QString& message) {
    m_base->sendChatMessage(message);
}

void PluginContext::requestTts(const QString& text, const QString& speakerId, int speed, int pitch, int volume) {
    m_base->requestTts(text, speakerId, speed, pitch, volume);
}

void PluginContext::sendToObs(const QString& action, const QJsonObject& payload) {
    m_base->sendToObs(action, payload);
}

void PluginContext::postDiscordWebhook(const QString& webhookUrl, const QJsonObject& payload) {
    m_base->postDiscordWebhook(webhookUrl, payload);
}

QString PluginContext::getPluginDirectory() const {
    QDir dir(m_base->getPluginDirectory());
    return dir.filePath(m_pluginId);
}

QString PluginContext::getCipherKey() const {
    return m_base->getCipherKey();
}

bool PluginContext::writeEncryptedFile(const QString& relativePath, const QByteArray& data) {
    if (relativePath.contains("..") || QFileInfo(relativePath).isAbsolute()) {
        Logger::instance().log("ERROR", "PluginContext", "writeEncryptedFile",
                               QString("Blocked unsafe file write path: %1").arg(relativePath));
        return false;
    }

    QMutexLocker locker(&m_mutex);
    QString absolutePath = QDir(getPluginDirectory()).filePath(relativePath);
    
    // Ensure parent directories exist
    QFileInfo fileInfo(absolutePath);
    QDir().mkpath(fileInfo.absolutePath());

    // Encrypt with TransCipher
    CipherResult result = CipherEngine::encrypt(data, getCipherKey().toUtf8(), AesMode::Mandatory);
    if (!result.isSuccess()) {
        Logger::instance().log("ERROR", "PluginContext", "writeEncryptedFile",
                               QString("Encryption failed for file: %1").arg(relativePath));
        return false;
    }

    QByteArray encryptedBase64 = result.data().toBase64();

    QFile file(absolutePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        Logger::instance().log("ERROR", "PluginContext", "writeEncryptedFile",
                               QString("Failed to open file for write: %1").arg(absolutePath));
        return false;
    }

    file.write(encryptedBase64);
    file.close();
    return true;
}

QByteArray PluginContext::readEncryptedFile(const QString& relativePath) {
    if (relativePath.contains("..") || QFileInfo(relativePath).isAbsolute()) {
        Logger::instance().log("ERROR", "PluginContext", "readEncryptedFile",
                               QString("Blocked unsafe file read path: %1").arg(relativePath));
        return QByteArray();
    }

    QMutexLocker locker(&m_mutex);
    QString absolutePath = QDir(getPluginDirectory()).filePath(relativePath);

    QFile file(absolutePath);
    if (!file.exists()) {
        return QByteArray();
    }

    if (!file.open(QIODevice::ReadOnly)) {
        Logger::instance().log("ERROR", "PluginContext", "readEncryptedFile",
                               QString("Failed to open file for read: %1").arg(absolutePath));
        return QByteArray();
    }

    QByteArray encryptedBase64 = file.readAll();
    file.close();

    QByteArray encryptedData = QByteArray::fromBase64(encryptedBase64);
    CipherResult result = CipherEngine::decrypt(encryptedData, getCipherKey().toUtf8());
    if (!result.isSuccess()) {
        Logger::instance().log("ERROR", "PluginContext", "readEncryptedFile",
                               QString("Decryption failed for file: %1").arg(relativePath));
        return QByteArray();
    }

    return result.data();
}

void PluginContext::writeLog(const QString& level, const QString& className, const QString& funcName, const QString& description) {
    m_base->writeLog(level, className, funcName, description);
}

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
    
    // PluginContext の生成 (プラグインごとに独立した暗号化/復号コンテキストを提供)
    PluginContext* pluginContext = new PluginContext(context, plugin->pluginId());
    
    // 初期化
    plugin->initialize(pluginContext);
    
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
    lp.context = pluginContext;
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
    
    // 4. DLLのアンロード (QPluginLoader::unload()は、内部でQSqlDatabaseなどの静的オブジェクトが使用されている場合にデッドロック/クラッシュを引き起こすため、プロセスの終了までメモリ上に維持します)
    bool ok = true;
    Logger::instance().log("INFO", "PluginLoader", "unloadPlugin", 
                           QString("Plugin metadata cleared, keeping DLL in memory to prevent static cleanup crash: %1").arg(filePath));
    
    if (lp.context) {
        delete lp.context;
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
