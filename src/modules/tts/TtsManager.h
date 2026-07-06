#pragma once
#include <QObject>
#include <QThread>
#include <QJsonObject>
#include <QMutex>
#include "ITtsClient.h"

class TtsManager : public QObject {
    Q_OBJECT
public:
    TtsManager();
    ~TtsManager();

    // 接続初期化・切り替え
    void startManager();
    void stopManager();

public slots:
    // 発話リクエスト (キュー処理などを考慮し、スレッド上で実行される)
    void requestSpeak(const QString& text, const QString& speakerId, int speed, int pitch, int volume);
    
    // 設定変更・エンジン切り替え
    void updateSettings(int activeEngineType, const QJsonObject& settings);

private:
    QThread* m_workerThread;
    ITtsClient* m_activeClient;
    int m_activeEngineType; // 0 = 棒読みちゃん, 1 = VOICEVOX
    QJsonObject m_currentSettings;
    QMutex m_mutex;
};
