#include "BouyomiClient.h"
#include <QDataStream>
#include <QByteArray>
#include "logger/Logger.h"

BouyomiClient::BouyomiClient() : m_host("localhost"), m_port(50001) {}

BouyomiClient::~BouyomiClient() {
    stop();
}

bool BouyomiClient::initialize(const QJsonObject& settings) {
    m_host = settings.value("host").toString("localhost");
    m_port = settings.value("port").toInt(50001);
    Logger::instance().log("INFO", "BouyomiClient", "initialize", 
                           QString("Initialized with Host: %1, Port: %2").arg(m_host).arg(m_port));
    return true;
}

void BouyomiClient::speak(const QString& text, const QString& speakerId, int speed, int pitch, int volume) {
    QTcpSocket socket;
    socket.connectToHost(m_host, m_port);
    if (!socket.waitForConnected(1000)) {
        Logger::instance().log("ERROR", "BouyomiClient", "speak", 
                               QString("Connection timed out to %1:%2").arg(m_host).arg(m_port));
        return;
    }
    
    QByteArray packet;
    QDataStream stream(&packet, QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);
    
    quint16 command = 1;
    qint16 speedVal = (speed < 0) ? -1 : static_cast<qint16>(speed);
    qint16 pitchVal = (pitch < 0) ? -1 : static_cast<qint16>(pitch);
    qint16 volumeVal = (volume < 0) ? -1 : static_cast<qint16>(volume);
    
    bool ok = false;
    quint16 voiceVal = speakerId.toUShort(&ok);
    if (!ok) {
        voiceVal = 0; // デフォルト声質
    }
    
    quint8 charset = 0; // UTF-8
    QByteArray textBytes = text.toUtf8();
    quint32 length = textBytes.length();
    
    stream << command;
    stream << speedVal;
    stream << pitchVal;
    stream << volumeVal;
    stream << voiceVal;
    stream << charset;
    stream << length;
    
    packet.append(textBytes);
    
    socket.write(packet);
    if (socket.waitForBytesWritten(1000)) {
        Logger::instance().log("INFO", "BouyomiClient", "speak", 
                               QString("Sent text: %1 (speed=%2, pitch=%3, volume=%4, voice=%5)")
                               .arg(text).arg(speedVal).arg(pitchVal).arg(volumeVal).arg(voiceVal));
    } else {
        Logger::instance().log("ERROR", "BouyomiClient", "speak", "Failed to write data to socket");
    }
    
    socket.disconnectFromHost();
}

void BouyomiClient::stop() {
    // 棒読みちゃん側はコマンド送信後すぐに切断するため、stop処理での切断操作は特になし
}
