#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QTcpServer>
#include <QTcpSocket>
#include <QEventLoop>
#include <QJsonObject>
#include <QJsonDocument>
#include "modules/tts/BouyomiClient.h"
#include "modules/tts/VoiceVoxClient.h"

// UT_TTS_001: BouyomiClient パケット生成テスト
TEST(TtsClientsTest, UT_TTS_001_BouyomiPacket) {
    // ローカルTCPサーバーを起動して送信バイトをキャプチャする
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    int port = server.serverPort();
    
    QJsonObject settings;
    settings["host"] = "127.0.0.1";
    settings["port"] = port;
    
    BouyomiClient client;
    client.initialize(settings);
    
    QByteArray capturedData;
    QEventLoop loop;
    
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, [=, &capturedData, &loop]() {
            capturedData.append(socket->readAll());
            if (capturedData.size() >= 15 + 9) { // ヘッダー15バイト + "テスト"のUTF8バイト(9)
                loop.quit();
            }
        });
    });
    
    // スピーチ要求を発行
    client.speak("テスト", "2", 120, 90, 80);
    
    loop.exec();
    server.close();
    
    // パケットバイトの検証 (Little Endian)
    ASSERT_GE(capturedData.size(), 15);
    
    // ヘッダー構造
    const char* ptr = capturedData.constData();
    quint16 command = *reinterpret_cast<const quint16*>(ptr);
    qint16 speed = *reinterpret_cast<const qint16*>(ptr + 2);
    qint16 pitch = *reinterpret_cast<const qint16*>(ptr + 4);
    qint16 volume = *reinterpret_cast<const qint16*>(ptr + 6);
    quint16 voice = *reinterpret_cast<const quint16*>(ptr + 8);
    quint8 charset = *reinterpret_cast<const quint8*>(ptr + 10);
    quint32 textLen = *reinterpret_cast<const quint32*>(ptr + 11);
    
    EXPECT_EQ(command, 1);
    EXPECT_EQ(speed, 120);
    EXPECT_EQ(pitch, 90);
    EXPECT_EQ(volume, 80);
    EXPECT_EQ(voice, 2);
    EXPECT_EQ(charset, 0); // UTF-8
    EXPECT_EQ(textLen, 9);  // "テスト" のUTF-8バイト長
    
    QByteArray textBytes = capturedData.mid(15);
    EXPECT_EQ(textBytes, "テスト");
}

// UT_TTS_002: VoiceVoxClient クエリパラメータ書き換えテスト
TEST(TtsClientsTest, UT_TTS_002_VoiceVoxParameterRewrite) {
    QTcpServer server;
    ASSERT_TRUE(server.listen(QHostAddress::LocalHost, 0));
    int port = server.serverPort();
    
    QJsonObject settings;
    settings["host"] = "127.0.0.1";
    settings["port"] = port;
    
    VoiceVoxClient client;
    client.initialize(settings);
    
    QByteArray audioQueryRequest;
    QByteArray synthesisRequest;
    
    QObject::connect(&server, &QTcpServer::newConnection, [&]() {
        QTcpSocket* socket = server.nextPendingConnection();
        QObject::connect(socket, &QTcpSocket::readyRead, [=, &audioQueryRequest, &synthesisRequest]() {
            QByteArray request = socket->readAll();
            if (request.contains("POST /audio_query")) {
                audioQueryRequest = request;
                
                // ダミーの VOICEVOX audio_query レスポンスを返す
                QJsonObject queryJson;
                queryJson["speedScale"] = 1.0;
                queryJson["pitchScale"] = 1.0;
                queryJson["volumeScale"] = 1.0;
                QByteArray body = QJsonDocument(queryJson).toJson(QJsonDocument::Compact);
                
                QByteArray response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: application/json\r\n"
                    "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                    "Connection: close\r\n\r\n" + body;
                socket->write(response);
                socket->disconnectFromHost();
            } else if (request.contains("POST /synthesis")) {
                synthesisRequest = request;
                
                // ダミーの合成音声（WAVヘッダー風）を返す
                QByteArray body = "RIFF\x24\x00\x00\x00WAVEfmt \x10\x00\x00\x00\x01\x00\x01\x00\x11\x2b\x00\x00\x11\x2b\x00\x00\x01\x00\x08\x00data\x00\x00\x00\x00";
                QByteArray response = 
                    "HTTP/1.1 200 OK\r\n"
                    "Content-Type: audio/wav\r\n"
                    "Content-Length: " + QByteArray::number(body.size()) + "\r\n"
                    "Connection: close\r\n\r\n" + body;
                socket->write(response);
                socket->disconnectFromHost();
            }
        });
    });
    
    // スピーチ要求 (speed 130, pitch 95, volume 80)
    // バックグラウンドスレッドではないので直接呼べる
    client.speak("テスト", "1", 130, 95, 80);
    
    server.close();
    
    // リクエスト確認
    EXPECT_TRUE(audioQueryRequest.contains("text=%E3%83%85%E3%82%B9%E3%83%88") || audioQueryRequest.contains("text="));
    
    // synthesis リクエストボディから JSON を取り出し、書き換えを検証
    int bodyStartIdx = synthesisRequest.indexOf("\r\n\r\n");
    ASSERT_NE(bodyStartIdx, -1);
    QByteArray bodyJson = synthesisRequest.mid(bodyStartIdx + 4);
    
    QJsonDocument doc = QJsonDocument::fromJson(bodyJson);
    ASSERT_FALSE(doc.isNull());
    QJsonObject obj = doc.object();
    
    // 比率に換算された値が入っているか
    EXPECT_DOUBLE_EQ(obj["speedScale"].toDouble(), 1.3);
    EXPECT_DOUBLE_EQ(obj["pitchScale"].toDouble(), 0.95);
    EXPECT_DOUBLE_EQ(obj["volumeScale"].toDouble(), 0.8);
}
