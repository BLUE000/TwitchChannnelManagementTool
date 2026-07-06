#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QWebSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QFile>
#include "modules/obs/ObsHttpWebSocketServer.h"

// UT_OBS_001: WebSocket 送信JSONフォーマットテスト
TEST(ObsServerTest, UT_OBS_001_WebSocketBroadcastJson) {
    // ログの有効化
    QString appDir = QCoreApplication::applicationDirPath();
    QString configPath = QDir(appDir).filePath("debug_config.json");
    QFile configFile(configPath);
    if (configFile.open(QIODevice::WriteOnly)) {
        QJsonObject obj;
        obj["enable_logging"] = true;
        configFile.write(QJsonDocument(obj).toJson());
        configFile.close();
    }

    ObsHttpWebSocketServer server;
    int testPort = 18081;
    server.startServer(testPort);
    
    QWebSocket client;
    QEventLoop loop;
    QByteArray receivedData;
    
    QObject::connect(&client, &QWebSocket::textMessageReceived, [&](const QString& msg) {
        receivedData = msg.toUtf8();
        loop.quit();
    });
    
    // クライアントを接続
    client.open(QUrl(QString("ws://localhost:%1").arg(testPort)));
    
    // 1. 接続完了を待つループ
    QEventLoop connectLoop;
    QObject::connect(&client, &QWebSocket::connected, &connectLoop, &QEventLoop::quit);
    QTimer::singleShot(2000, &connectLoop, &QEventLoop::quit);
    connectLoop.exec();
    
    // イベントループを少し回して、サーバー側の新接続登録を完了させる
    QEventLoop waitLoop;
    QTimer::singleShot(100, &waitLoop, &QEventLoop::quit);
    waitLoop.exec();
    
    // 2. ブロードキャストを発行 (この時点ではサーバー側登録が完了)
    QJsonObject data;
    data["comment"] = "Hello OBS";
    data["displayName"] = "UserA";
    server.broadcastToObs("comments", data);
    
    // 3. メッセージ受信を待つループ
    QTimer::singleShot(2000, &loop, &QEventLoop::quit);
    loop.exec();
    
    client.close();
    server.stopServer();
    
    // 受信したJSONをパース
    ASSERT_FALSE(receivedData.isEmpty());
    QJsonDocument doc = QJsonDocument::fromJson(receivedData);
    ASSERT_TRUE(doc.isObject());
    
    QJsonObject obj = doc.object();
    EXPECT_EQ(obj["type"].toString(), "comments");
    
    QJsonObject innerData = obj["data"].toObject();
    EXPECT_EQ(innerData["comment"].toString(), "Hello OBS");
    EXPECT_EQ(innerData["displayName"].toString(), "UserA");
    
    QFile::remove(configPath);
}
