#include "AppController.h"
#include <QCoreApplication>
#include <QDir>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QJsonDocument>
#include "logger/Logger.h"
#include "SignalDispatcher.h"

AppController::AppController(QObject* parent)
    : QObject(parent)
    , m_pluginLoader(nullptr)
    , m_twitchCollector(nullptr)
    , m_obsServer(nullptr)
    , m_ttsManager(nullptr)
    , m_networkManager(nullptr)
    , m_tickTimer(nullptr)
    , m_cipherKey("TCMT_DefaultSecKey2026")
{
}

AppController::~AppController() {
    stopAllServices();
    
    // プラグインローダーを先に削除して、全プラグインを安全にアンロードさせる
    if (m_pluginLoader) {
        delete m_pluginLoader;
        m_pluginLoader = nullptr;
    }
    
    if (m_twitchCollector) {
        delete m_twitchCollector;
        m_twitchCollector = nullptr;
    }
    if (m_obsServer) {
        delete m_obsServer;
        m_obsServer = nullptr;
    }
    if (m_ttsManager) {
        delete m_ttsManager;
        m_ttsManager = nullptr;
    }
    if (m_networkManager) {
        m_networkManager->deleteLater();
        m_networkManager = nullptr;
    }
    if (m_tickTimer) {
        delete m_tickTimer;
        m_tickTimer = nullptr;
    }
}

void AppController::initialize() {
    Logger::instance().log("INFO", "AppController", "initialize", "Initializing AppController...");
    
    m_pluginLoader = new PluginLoader(this);
    m_twitchCollector = new TwitchEventCollector();
    m_obsServer = new ObsHttpWebSocketServer();
    m_ttsManager = new TtsManager();
    m_networkManager = new QNetworkAccessManager(this);
    
    m_tickTimer = new QTimer(this);
    connect(m_tickTimer, &QTimer::timeout, this, &AppController::on1sTimerTimeout);
    
    // Twitchコレクターのデータ収集シグナルをSignalDispatcherへ紐付ける
    connect(m_twitchCollector, &TwitchEventCollector::commentReceived, 
            &SignalDispatcher::instance(), &SignalDispatcher::dispatchComment);
            
    connect(m_twitchCollector, &TwitchEventCollector::rewardRedeemed, 
            &SignalDispatcher::instance(), &SignalDispatcher::dispatchReward);
            
    // 各スレッドの起動
    m_twitchCollector->startCollectorThread();
    m_ttsManager->startManager();
}

void AppController::startAllServices(quint16 obsPort) {
    Logger::instance().log("INFO", "AppController", "startAllServices", 
                           QString("Starting all services. OBS port=%1").arg(obsPort));
    
    m_obsServer->startServerThread(obsPort);
    
    if (m_tickTimer && !m_tickTimer->isActive()) {
        m_tickTimer->start(1000); // 1秒間隔のタイマー開始
    }
}

void AppController::stopAllServices() {
    Logger::instance().log("INFO", "AppController", "stopAllServices", "Stopping all services...");
    
    if (m_tickTimer) {
        m_tickTimer->stop();
    }
    
    if (m_obsServer) {
        m_obsServer->stopServerThread();
    }
    
    if (m_twitchCollector) {
        QMetaObject::invokeMethod(m_twitchCollector, "disconnectFromTwitch", Qt::QueuedConnection);
    }
}

void AppController::sendChatMessage(const QString& message) {
    QJsonObject payload;
    payload["message"] = message;
    QCoreApplication::postEvent(this, new PluginNotifyEvent(PluginNotifyEvent::TypeSendChat, payload));
}

void AppController::requestTts(const QString& text, const QString& speakerId, int speed, int pitch, int volume) {
    QJsonObject payload;
    payload["text"] = text;
    payload["speakerId"] = speakerId;
    payload["speed"] = speed;
    payload["pitch"] = pitch;
    payload["volume"] = volume;
    QCoreApplication::postEvent(this, new PluginNotifyEvent(PluginNotifyEvent::TypeTtsSpeak, payload));
}

void AppController::sendToObs(const QString& action, const QJsonObject& payload) {
    QJsonObject eventPayload;
    eventPayload["action"] = action;
    eventPayload["data"] = payload;
    QCoreApplication::postEvent(this, new PluginNotifyEvent(PluginNotifyEvent::TypeObsBroadcast, eventPayload));
}

void AppController::postDiscordWebhook(const QString& webhookUrl, const QJsonObject& payload) {
    QJsonObject eventPayload;
    eventPayload["url"] = webhookUrl;
    eventPayload["payload"] = payload;
    QCoreApplication::postEvent(this, new PluginNotifyEvent(PluginNotifyEvent::TypeDiscordWebhook, eventPayload));
}

QList<TwitchRewardInfo> AppController::getChannelPointRewards() {
    if (!m_twitchCollector) return {};
    QList<TwitchRewardInfo> rewards;
    QMetaObject::invokeMethod(m_twitchCollector, "getChannelPointRewards",
                              Qt::BlockingQueuedConnection,
                              Q_RETURN_ARG(QList<TwitchRewardInfo>, rewards));
    return rewards;
}

QString AppController::getPluginDirectory() const {
    QString appDir = QCoreApplication::applicationDirPath();
    return QDir(appDir).filePath("plugins");
}

QString AppController::getCipherKey() const {
    return m_cipherKey;
}

void AppController::on1sTimerTimeout() {
    SignalDispatcher::instance().dispatchTick();
    emit tick1s();
}

void AppController::customEvent(QEvent* event) {
    if (event->type() >= QEvent::User + 100) {
        PluginNotifyEvent* pEvent = static_cast<PluginNotifyEvent*>(event);
        QJsonObject payload = pEvent->payload();
        
        switch (pEvent->type()) {
            case PluginNotifyEvent::TypeSendChat: {
                QString msg = payload.value("message").toString();
                QMetaObject::invokeMethod(m_twitchCollector, "postChatMessage", Qt::QueuedConnection, Q_ARG(QString, msg));
                break;
            }
            case PluginNotifyEvent::TypeTtsSpeak: {
                QString text = payload.value("text").toString();
                QString speakerId = payload.value("speakerId").toString();
                int speed = payload.value("speed").toInt(100);
                int pitch = payload.value("pitch").toInt(100);
                int volume = payload.value("volume").toInt(50);
                m_ttsManager->requestSpeak(text, speakerId, speed, pitch, volume);
                break;
            }
            case PluginNotifyEvent::TypeObsBroadcast: {
                QString action = payload.value("action").toString();
                QJsonObject data = payload.value("data").toObject();
                QMetaObject::invokeMethod(m_obsServer, "broadcastToObs", Qt::QueuedConnection,
                                          Q_ARG(QString, action), Q_ARG(QJsonObject, data));
                break;
            }
            case PluginNotifyEvent::TypeDiscordWebhook: {
                QString url = payload.value("url").toString();
                QJsonObject body = payload.value("payload").toObject();
                performDiscordWebhook(url, body);
                break;
            }
            default:
                break;
        }
    }
    QObject::customEvent(event);
}

void AppController::performDiscordWebhook(const QString& url, const QJsonObject& payload) {
    if (!m_networkManager) return;
    
    QNetworkRequest request;
    request.setUrl(QUrl(url));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QJsonDocument doc(payload);
    QByteArray body = doc.toJson(QJsonDocument::Compact);
    
    QNetworkReply* reply = m_networkManager->post(request, body);
    connect(reply, &QNetworkReply::finished, this, [reply]() {
        if (reply->error() != QNetworkReply::NoError) {
            Logger::instance().log("ERROR", "AppController", "performDiscordWebhook", 
                                   QString("Discord Webhook post failed: %1").arg(reply->errorString()));
        } else {
            Logger::instance().log("INFO", "AppController", "performDiscordWebhook", 
                                   "Discord Webhook post completed successfully");
        }
        reply->deleteLater();
    });
}

bool AppController::writeEncryptedFile(const QString& relativePath, const QByteArray& data) {
    Q_UNUSED(relativePath);
    Q_UNUSED(data);
    return false;
}

QByteArray AppController::readEncryptedFile(const QString& relativePath) {
    Q_UNUSED(relativePath);
    return QByteArray();
}

void AppController::writeLog(const QString& level, const QString& className, const QString& funcName, const QString& description) {
    Logger::instance().log(level, className, funcName, description);
}
