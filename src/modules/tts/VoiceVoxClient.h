#pragma once
#include "ITtsClient.h"
#include <QString>
#include <QJsonObject>
#include <QNetworkAccessManager>

class VoiceVoxClient : public ITtsClient {
public:
    VoiceVoxClient();
    ~VoiceVoxClient() override;

    bool initialize(const QJsonObject& settings) override;
    void speak(const QString& text, const QString& speakerId, int speed, int pitch, int volume) override;
    void stop() override;

private:
    QString m_host;
    int m_port;
    QNetworkAccessManager* m_networkManager;
};
