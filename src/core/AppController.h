#pragma once
#include <QObject>
#include <QEvent>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QTimer>
#include "shared/plugin_interface.h"
#include "core/PluginLoader.h"
#include "modules/twitch/TwitchEventCollector.h"
#include "modules/obs/ObsHttpWebSocketServer.h"
#include "modules/tts/TtsManager.h"

class PluginNotifyEvent : public QEvent {
public:
    enum RequestType {
        TypeTtsSpeak = QEvent::User + 100, // TTS読み上げ要求
        TypeObsBroadcast,                  // OBSオーバーレイへの描画・演出要求
        TypeSendChat,                      // Twitchへのチャット送信要求
        TypeDiscordWebhook                 // Discord Webhook送信要求
    };

    PluginNotifyEvent(RequestType type, const QJsonObject& payload)
        : QEvent(static_cast<QEvent::Type>(type)), m_payload(payload) {}

    QJsonObject payload() const { return m_payload; }

private:
    QJsonObject m_payload;
};

class AppController : public QObject, public ICoreContext {
    Q_OBJECT
public:
    AppController(QObject* parent = nullptr);
    ~AppController() override;

    void initialize();
    void startAllServices(quint16 obsPort);
    void stopAllServices();

    // --- ICoreContext インターフェースの実装 ---
    void sendChatMessage(const QString& message) override;
    void requestTts(const QString& text, const QString& speakerId, int speed, int pitch, int volume) override;
    void sendToObs(const QString& action, const QJsonObject& payload) override;
    void postDiscordWebhook(const QString& webhookUrl, const QJsonObject& payload) override;
    QList<TwitchRewardInfo> getChannelPointRewards() override;
    QString getPluginDirectory() const override;
    QString getCipherKey() const override;
    bool writeEncryptedFile(const QString& relativePath, const QByteArray& data) override;
    QByteArray readEncryptedFile(const QString& relativePath) override;
    void writeLog(const QString& level, const QString& className, const QString& funcName, const QString& description) override;

    // モジュールへのゲッター
    PluginLoader* pluginLoader() const { return m_pluginLoader; }
    TwitchEventCollector* twitchCollector() const { return m_twitchCollector; }
    ObsHttpWebSocketServer* obsServer() const { return m_obsServer; }
    TtsManager* ttsManager() const { return m_ttsManager; }

signals:
    void tick1s();

protected:
    void customEvent(QEvent* event) override;

private slots:
    void on1sTimerTimeout();

private:
    void performDiscordWebhook(const QString& url, const QJsonObject& payload);

    PluginLoader* m_pluginLoader;
    TwitchEventCollector* m_twitchCollector;
    ObsHttpWebSocketServer* m_obsServer;
    TtsManager* m_ttsManager;
    QNetworkAccessManager* m_networkManager;
    
    QTimer* m_tickTimer;
    QString m_cipherKey;
};
