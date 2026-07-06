#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QDir>
#include <QFile>
#include "core/AppController.h"
#include "core/SignalDispatcher.h"

// IT_DWN_001: Twitchコメント受信 ➔ プラグイン伝播の結合テスト
TEST(IntegrationTest, IT_DWN_001_TwitchCommentPropagation) {
    SignalDispatcher& dispatcher = SignalDispatcher::instance();
    
    TwitchComment comment;
    comment.id = "msg-123";
    comment.userId = "user-123";
    comment.username = "viewer";
    comment.displayName = "リスナー";
    comment.comment = "こんにちは";
    comment.timestamp = 123456789;
    
    bool commentReceived = false;
    QObject::connect(&dispatcher, &SignalDispatcher::commentReceived, [&](const TwitchComment& c) {
        EXPECT_EQ(c.id, "msg-123");
        EXPECT_EQ(c.comment, "こんにちは");
        commentReceived = true;
    });
    
    dispatcher.dispatchComment(comment);
    
    EXPECT_TRUE(commentReceived);
}

// IT_UP_003: プラグイン ➔ コア ➔ Discord Webhook 送信の結合テスト
TEST(IntegrationTest, IT_UP_003_DiscordWebhookPost) {
    // ローカルモック Webhook サーバーを起動
    QTcpServer webhookServer;
    ASSERT_TRUE(webhookServer.listen(QHostAddress::LocalHost, 0));
    int port = webhookServer.serverPort();
    
    QString mockWebhookUrl = QString("http://localhost:%1/webhook").arg(port);
    
    AppController controller;
    controller.initialize();
    
    QEventLoop loop;
    QByteArray capturedRequestData;
    
    QObject::connect(&webhookServer, &QTcpServer::newConnection, [&]() {
        QTcpSocket* socket = webhookServer.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, [=, &capturedRequestData, &loop]() {
            capturedRequestData.append(socket->readAll());
            
            // HTTP 200 OK 応答を返す
            QByteArray response = "HTTP/1.1 204 No Content\r\nConnection: close\r\n\r\n";
            socket->write(response);
            socket->disconnectFromHost();
            loop.quit();
        });
    });
    
    QJsonObject payload;
    payload["content"] = "Webhook Test Message";
    
    // Webhook送信要求を呼び出し
    controller.postDiscordWebhook(mockWebhookUrl, payload);
    
    // タイムアウト設定してイベントループを回す
    QTimer::singleShot(3000, &loop, &QEventLoop::quit);
    loop.exec();
    
    webhookServer.close();
    
    // リクエスト検証
    ASSERT_FALSE(capturedRequestData.isEmpty());
    EXPECT_TRUE(capturedRequestData.contains("POST /webhook"));
    EXPECT_TRUE(capturedRequestData.contains("content"));
    EXPECT_TRUE(capturedRequestData.contains("Webhook Test Message"));
}

// IT_LFC_004: OBS HTTPサーバーによる静的ファイル配信テスト
TEST(IntegrationTest, IT_LFC_004_ObsHttpStaticFileDelivery) {
    // テスト用のダミーアセットを作成
    QString appDir = QCoreApplication::applicationDirPath();
    QString testAssetPath = QDir(appDir).filePath("assets/overlay/TestPlugin/default/index.html");
    QDir().mkpath(QFileInfo(testAssetPath).absolutePath());
    
    QFile file(testAssetPath);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("<html>Test Delivery</html>");
    file.close();
    
    ObsHttpWebSocketServer server;
    int port = 19082;
    server.startServer(port);
    
    // QNetworkAccessManagerを使ってリクエストを投げる
    QNetworkAccessManager manager;
    QUrl url(QString("http://localhost:%1/overlay/TestPlugin/default/index.html").arg(port));
    QNetworkReply* reply = manager.get(QNetworkRequest(url));
    
    QEventLoop loop;
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    server.stopServer();
    
    EXPECT_EQ(reply->error(), QNetworkReply::NoError);
    EXPECT_EQ(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    EXPECT_EQ(reply->header(QNetworkRequest::ContentTypeHeader).toString(), "text/html; charset=utf-8");
    
    QByteArray body = reply->readAll();
    EXPECT_EQ(body, "<html>Test Delivery</html>");
    
    reply->deleteLater();
    
    // 存在しないファイルの404チェック
    server.startServer(port);
    QUrl url404(QString("http://localhost:%1/overlay/TestPlugin/default/notfound.html").arg(port));
    QNetworkReply* reply404 = manager.get(QNetworkRequest(url404));
    
    QObject::connect(reply404, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    
    server.stopServer();
    
    EXPECT_EQ(reply404->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 404);
    reply404->deleteLater();
    
    // クリーニング
    QFile::remove(testAssetPath);
}
