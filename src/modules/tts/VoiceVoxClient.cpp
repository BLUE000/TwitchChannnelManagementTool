#include "VoiceVoxClient.h"
#include <QEventLoop>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrl>
#include "logger/Logger.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <mmsystem.h>
#endif

VoiceVoxClient::VoiceVoxClient() : m_host("localhost"), m_port(50021), m_networkManager(nullptr) {}

VoiceVoxClient::~VoiceVoxClient() {
    stop();
    if (m_networkManager) {
        m_networkManager->deleteLater();
    }
}

bool VoiceVoxClient::initialize(const QJsonObject& settings) {
    m_host = settings.value("host").toString("localhost");
    m_port = settings.value("port").toInt(50021);
    
    if (!m_networkManager) {
        m_networkManager = new QNetworkAccessManager();
    }
    
    Logger::instance().log("INFO", "VoiceVoxClient", "initialize", 
                           QString("Initialized with Host: %1, Port: %2").arg(m_host).arg(m_port));
    return true;
}

void VoiceVoxClient::speak(const QString& text, const QString& speakerId, int speed, int pitch, int volume) {
    if (!m_networkManager) {
        return;
    }
    
    // 1. audio_query のリクエスト
    QUrl queryUrl(QString("http://%1:%2/audio_query").arg(m_host).arg(m_port));
    QUrlQuery urlQuery;
    urlQuery.addQueryItem("text", text);
    urlQuery.addQueryItem("speaker", speakerId);
    queryUrl.setQuery(urlQuery);
    
    QNetworkRequest queryRequest(queryUrl);
    queryRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply* queryReply = m_networkManager->post(queryRequest, QByteArray());
    QEventLoop loop;
    QObject::connect(queryReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (queryReply->error() != QNetworkReply::NoError) {
        Logger::instance().log("ERROR", "VoiceVoxClient", "speak", 
                               QString("audio_query failed: %1").arg(queryReply->errorString()));
        queryReply->deleteLater();
        return;
    }
    
    QByteArray queryJsonData = queryReply->readAll();
    queryReply->deleteLater();
    
    // 2. パラメータの書き換え
    QJsonDocument doc = QJsonDocument::fromJson(queryJsonData);
    if (doc.isNull() || !doc.isObject()) {
        Logger::instance().log("ERROR", "VoiceVoxClient", "speak", "Invalid JSON from audio_query");
        return;
    }
    
    QJsonObject queryObj = doc.object();
    
    // speed/pitch/volume はパーセンテージ（例：100が標準）で渡されるので、VOICEVOX用のスケール値（1.0が標準）に変換
    if (speed >= 0) {
        queryObj["speedScale"] = speed / 100.0;
    }
    if (pitch >= 0) {
        queryObj["pitchScale"] = pitch / 100.0;
    }
    if (volume >= 0) {
        queryObj["volumeScale"] = volume / 100.0;
    }
    
    QJsonDocument modifiedDoc(queryObj);
    QByteArray body = modifiedDoc.toJson(QJsonDocument::Compact);
    
    // 3. synthesis のリクエスト
    QUrl synthesisUrl(QString("http://%1:%2/synthesis").arg(m_host).arg(m_port));
    QUrlQuery synthQuery;
    synthQuery.addQueryItem("speaker", speakerId);
    synthesisUrl.setQuery(synthQuery);
    
    QNetworkRequest synthRequest(synthesisUrl);
    synthRequest.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    
    QNetworkReply* synthReply = m_networkManager->post(synthRequest, body);
    QObject::connect(synthReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    if (synthReply->error() != QNetworkReply::NoError) {
        Logger::instance().log("ERROR", "VoiceVoxClient", "speak", 
                               QString("synthesis failed: %1").arg(synthReply->errorString()));
        synthReply->deleteLater();
        return;
    }
    
    QByteArray wavData = synthReply->readAll();
    synthReply->deleteLater();
    
    // 4. 音声の再生
    if (wavData.isEmpty()) {
        Logger::instance().log("ERROR", "VoiceVoxClient", "speak", "Received empty audio data");
        return;
    }
    
    Logger::instance().log("INFO", "VoiceVoxClient", "speak", 
                           QString("Playing synthesized voice for text: %1").arg(text));

#ifdef Q_OS_WIN
    PlaySoundA(reinterpret_cast<LPCSTR>(wavData.constData()), NULL, SND_MEMORY | SND_SYNC | SND_NODEFAULT);
#else
    Logger::instance().log("WARNING", "VoiceVoxClient", "speak", "Playback is only supported on Windows in this build");
#endif
}

void VoiceVoxClient::stop() {
#ifdef Q_OS_WIN
    PlaySoundA(NULL, NULL, 0); // 再生停止
#endif
}
