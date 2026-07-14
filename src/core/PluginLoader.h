#pragma once
#include <QObject>
#include <QPluginLoader>
#include <QMap>
#include <QList>
#include <QString>
#include <QWidget>
#include <QMutex>
#include <QJsonObject>
#include <QPointer>
#include "shared/plugin_interface.h"

struct LoadedPlugin {
    QString filePath;
    QPluginLoader* loader;
    IChannelPlugin* instance;
    QPointer<QWidget> widget;
    ICoreContext* context;
    QList<QMetaObject::Connection> connections;
};

class PluginContext : public ICoreContext {
public:
    PluginContext(ICoreContext* baseContext, const QString& pluginId);
    ~PluginContext() override = default;

    void sendChatMessage(const QString& message) override;
    void requestTts(const QString& text, const QString& speakerId, int speed, int pitch, int volume) override;
    void sendToObs(const QString& action, const QJsonObject& payload) override;
    void postDiscordWebhook(const QString& webhookUrl, const QJsonObject& payload) override;
    QString getPluginDirectory() const override;
    QString getCipherKey() const override;

    bool writeEncryptedFile(const QString& relativePath, const QByteArray& data) override;
    QByteArray readEncryptedFile(const QString& relativePath) override;
    void writeLog(const QString& level, const QString& className, const QString& funcName, const QString& description) override;

private:
    ICoreContext* m_base;
    QString m_pluginId;
    mutable QMutex m_mutex;
};

class PluginLoader : public QObject {
    Q_OBJECT
public:
    PluginLoader(QObject* parent = nullptr);
    ~PluginLoader();

    // plugins/ ディレクトリ内の DLL を走査する
    QList<QString> scanPlugins();

    // 個別のプラグインをロードする
    IChannelPlugin* loadPlugin(const QString& filePath, ICoreContext* context, QWidget* uiParent = nullptr);

    // 個別のプラグインをアンロードする
    bool unloadPlugin(const QString& filePath);

    // 現在ロードされているプラグインのリストを取得
    QList<LoadedPlugin> loadedPlugins() const;

protected:
    void extractDefaultAssets(IChannelPlugin* plugin);

private:

    QMap<QString, LoadedPlugin> m_loadedPlugins; // キー: DLLの絶対パス
};
