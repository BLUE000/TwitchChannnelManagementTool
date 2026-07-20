#pragma once
#include <QObject>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocket>
#include <QSslSocket>
#include <QJsonObject>
#include <QTimer>
#include "shared/plugin_interface.h"

class TwitchEventCollector : public QObject {
    Q_OBJECT
public:
    TwitchEventCollector();
    ~TwitchEventCollector();

    void startCollectorThread();
    void stopCollectorThread();

signals:
    void commentReceived(const TwitchComment& comment);
    void rewardRedeemed(const TwitchRewardRedemption& redemption);
    void connectionStatusChanged(bool connected, const QString& accountName);
    void tokenRetrieved(const QString& token);

public slots:
    void connectToTwitch(const QString& channelName, const QString& oauthToken);
    void disconnectFromTwitch();
    void postChatMessage(const QString& message);
    QList<TwitchRewardInfo> getChannelPointRewards();

private slots:
    // 臨時認証サーバー用
    void onNewAuthConnection();
    void onAuthReadyRead();

    // 接続維持・再試行用
    void onIrcConnected();
    void onIrcReadyRead();
    void onIrcDisconnected();
    
    void onEventSubConnected();
    void onEventSubTextMessageReceived(const QString& message);
    void onEventSubDisconnected();

    // テストモード用
    void onTestTimerTick();

private:
    void startAuthServer();
    void stopAuthServer();
    void connectIrc();
    void connectEventSub();
    void parseIrcMessage(const QString& rawMessage);

    QThread* m_workerThread;
    
    // 臨時認証サーバー
    QTcpServer* m_authServer;
    
    // 実通信用
    QSslSocket* m_ircSocket;
    QWebSocket* m_eventSubWebSocket;
    
    QString m_channelName;
    QString m_oauthToken;
    bool m_connected;

    // テストモード用タイマー
    QTimer* m_testTimer;
    int m_testMessageCounter;
};
