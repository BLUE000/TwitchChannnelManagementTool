#pragma once
#include <QString>
#include <QJsonObject>

class ITtsClient {
public:
    virtual ~ITtsClient() = default;
    
    // 各固有設定を用いた接続初期化
    virtual bool initialize(const QJsonObject& settings) = 0;
    
    // 発話要求
    virtual void speak(const QString& text, const QString& speakerId, int speed, int pitch, int volume) = 0;
    
    // 発話強制停止・切断
    virtual void stop() = 0;
};
