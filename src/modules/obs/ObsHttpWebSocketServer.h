#pragma once
#include <QObject>
#include <QThread>
#include <QTcpServer>
#include <QTcpSocket>
#include <QWebSocketServer>
#include <QWebSocket>
#include <QList>
#include <QJsonObject>
#include <QMutex>

class ObsHttpWebSocketServer : public QObject {
    Q_OBJECT
public:
    ObsHttpWebSocketServer();
    ~ObsHttpWebSocketServer();

    void startServerThread(quint16 port);
    void stopServerThread();

public slots:
    void startServer(quint16 port);
    void stopServer();
    void broadcastToObs(const QString& actionType, const QJsonObject& payload);

private slots:
    void onNewTcpConnection();
    void onTcpReadyRead();
    void onTcpDisconnected();
    
    void onNewWebSocketConnection();
    void onWebSocketDisconnected();
    void onWebSocketMessageReceived(const QString& message);

private:
    void handleHttpRequest(QTcpSocket* socket, const QByteArray& requestData);
    QString getContentType(const QString& filePath);

    QThread* m_workerThread;
    QTcpServer* m_tcpServer;
    QWebSocketServer* m_webSocketServer;
    QList<QWebSocket*> m_clients;
    QMutex m_mutex;
    quint16 m_port;
};
