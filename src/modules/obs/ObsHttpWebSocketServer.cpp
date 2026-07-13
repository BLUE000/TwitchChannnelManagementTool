#include "ObsHttpWebSocketServer.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QUrl>
#include <QJsonDocument>
#include "logger/Logger.h"

ObsHttpWebSocketServer::ObsHttpWebSocketServer() 
    : m_workerThread(nullptr)
    , m_tcpServer(nullptr)
    , m_webSocketServer(nullptr)
    , m_port(58081)
{
}

ObsHttpWebSocketServer::~ObsHttpWebSocketServer() {
    stopServerThread();
}

void ObsHttpWebSocketServer::startServerThread(quint16 port) {
    if (!m_workerThread) {
        m_workerThread = new QThread();
        this->moveToThread(m_workerThread);
        connect(m_workerThread, &QThread::started, this, [this, port]() {
            this->startServer(port);
        });
        m_workerThread->start();
    }
}

void ObsHttpWebSocketServer::stopServerThread() {
    if (m_workerThread) {
        QMetaObject::invokeMethod(this, "stopServer", Qt::BlockingQueuedConnection);
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
        m_workerThread = nullptr;
    }
}

void ObsHttpWebSocketServer::startServer(quint16 port) {
    m_port = port;
    
    m_tcpServer = new QTcpServer(this);
    m_webSocketServer = new QWebSocketServer("TwitchChannelManagementToolOBS", QWebSocketServer::NonSecureMode, this);
    
    connect(m_tcpServer, &QTcpServer::newConnection, this, &ObsHttpWebSocketServer::onNewTcpConnection);
    connect(m_webSocketServer, &QWebSocketServer::newConnection, this, &ObsHttpWebSocketServer::onNewWebSocketConnection);
    
    if (m_tcpServer->listen(QHostAddress::Any, port)) {
        Logger::instance().log("INFO", "ObsHttpWebSocketServer", "startServer", 
                               QString("HTTP/WebSocket Server listening on port %1").arg(port));
    } else {
        Logger::instance().log("ERROR", "ObsHttpWebSocketServer", "startServer", 
                               QString("Failed to start HTTP/WebSocket Server on port %1: %2")
                               .arg(port).arg(m_tcpServer->errorString()));
    }
}

void ObsHttpWebSocketServer::stopServer() {
    QMutexLocker locker(&m_mutex);
    for (QWebSocket* client : m_clients) {
        client->close();
        client->deleteLater();
    }
    m_clients.clear();
    
    if (m_tcpServer) {
        m_tcpServer->close();
        m_tcpServer->deleteLater();
        m_tcpServer = nullptr;
    }
    if (m_webSocketServer) {
        m_webSocketServer->close();
        m_webSocketServer->deleteLater();
        m_webSocketServer = nullptr;
    }
    Logger::instance().log("INFO", "ObsHttpWebSocketServer", "stopServer", "HTTP/WebSocket Server stopped");
}

void ObsHttpWebSocketServer::onNewTcpConnection() {
    QTcpSocket* socket = m_tcpServer->nextPendingConnection();
    if (socket) {
        connect(socket, &QTcpSocket::readyRead, this, &ObsHttpWebSocketServer::onTcpReadyRead);
        connect(socket, &QTcpSocket::disconnected, this, &ObsHttpWebSocketServer::onTcpDisconnected);
    }
}

void ObsHttpWebSocketServer::onTcpReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    
    // データを取り込まずに先頭を覗き見 (peek) する
    QByteArray peekData = socket->peek(4096);
    
    // WebSocketアップグレード判定
    if (peekData.contains("Upgrade: websocket") || peekData.contains("upgrade: websocket")) {
        socket->disconnect(this); // HTTP用のシグナル接続を切断
        m_webSocketServer->handleConnection(socket);
        return;
    }
    
    // 通常のHTTPリクエスト処理 (実際にデータを読み取る)
    QByteArray requestData = socket->readAll();
    handleHttpRequest(socket, requestData);
}

void ObsHttpWebSocketServer::onTcpDisconnected() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (socket) {
        socket->deleteLater();
    }
}

void ObsHttpWebSocketServer::handleHttpRequest(QTcpSocket* socket, const QByteArray& requestData) {
    QString requestStr = QString::fromUtf8(requestData);
    QStringList lines = requestStr.split("\r\n");
    if (lines.isEmpty()) {
        socket->close();
        return;
    }
    
    QStringList firstLineTokens = lines[0].split(" ");
    if (firstLineTokens.size() < 2) {
        socket->close();
        return;
    }
    
    QString method = firstLineTokens[0];
    QString path = firstLineTokens[1];
    
    // クエリパラメータを削除
    int queryIdx = path.indexOf('?');
    if (queryIdx != -1) {
        path = path.left(queryIdx);
    }
    
    // URLエンコーディングのデコード
    path = QUrl::fromPercentEncoding(path.toUtf8());
    
    if (method != "GET") {
        QByteArray response = "HTTP/1.1 405 Method Not Allowed\r\nConnection: close\r\n\r\n";
        socket->write(response);
        socket->disconnectFromHost();
        return;
    }
    
    if (path.startsWith("/overlay/")) {
        QString appDir = QCoreApplication::applicationDirPath();
        // "/overlay/..." のパスを "assets/overlay/..." にマップする
        QString physicalPath = QDir(appDir).filePath("assets" + path);
        
        QFile file(physicalPath);
        if (file.exists() && file.open(QIODevice::ReadOnly)) {
            QByteArray fileData = file.readAll();
            file.close();
            
            QString contentType = getContentType(physicalPath);
            QByteArray response;
            response.append("HTTP/1.1 200 OK\r\n");
            response.append("Content-Type: " + contentType.toUtf8() + "\r\n");
            response.append("Content-Length: " + QByteArray::number(fileData.size()) + "\r\n");
            response.append("Access-Control-Allow-Origin: *\r\n");
            response.append("Connection: close\r\n\r\n");
            
            socket->write(response);
            socket->write(fileData);
            socket->disconnectFromHost();
            return;
        }
    }
    
    // 404 Not Found
    QByteArray response = "HTTP/1.1 404 Not Found\r\nContent-Type: text/plain\r\nConnection: close\r\n\r\nFile Not Found";
    socket->write(response);
    socket->disconnectFromHost();
}

QString ObsHttpWebSocketServer::getContentType(const QString& filePath) {
    if (filePath.endsWith(".html", Qt::CaseInsensitive)) return "text/html; charset=utf-8";
    if (filePath.endsWith(".css", Qt::CaseInsensitive)) return "text/css; charset=utf-8";
    if (filePath.endsWith(".js", Qt::CaseInsensitive)) return "application/javascript; charset=utf-8";
    if (filePath.endsWith(".png", Qt::CaseInsensitive)) return "image/png";
    if (filePath.endsWith(".jpg", Qt::CaseInsensitive) || filePath.endsWith(".jpeg", Qt::CaseInsensitive)) return "image/jpeg";
    if (filePath.endsWith(".gif", Qt::CaseInsensitive)) return "image/gif";
    if (filePath.endsWith(".svg", Qt::CaseInsensitive)) return "image/svg+xml; charset=utf-8";
    if (filePath.endsWith(".json", Qt::CaseInsensitive)) return "application/json; charset=utf-8";
    return "application/octet-stream";
}

void ObsHttpWebSocketServer::onNewWebSocketConnection() {
    QMutexLocker locker(&m_mutex);
    QWebSocket* client = m_webSocketServer->nextPendingConnection();
    if (client) {
        m_clients.append(client);
        connect(client, &QWebSocket::textMessageReceived, this, &ObsHttpWebSocketServer::onWebSocketMessageReceived);
        connect(client, &QWebSocket::disconnected, this, &ObsHttpWebSocketServer::onWebSocketDisconnected);
        Logger::instance().log("INFO", "ObsHttpWebSocketServer", "onNewWebSocketConnection", 
                               QString("New WebSocket Client connected: %1").arg(client->peerAddress().toString()));
    }
}

void ObsHttpWebSocketServer::onWebSocketDisconnected() {
    QMutexLocker locker(&m_mutex);
    QWebSocket* client = qobject_cast<QWebSocket*>(sender());
    if (client) {
        m_clients.removeAll(client);
        client->deleteLater();
        Logger::instance().log("INFO", "ObsHttpWebSocketServer", "onWebSocketDisconnected", "WebSocket Client disconnected");
    }
}

void ObsHttpWebSocketServer::onWebSocketMessageReceived(const QString& message) {
    // クライアントからの受信メッセージ処理 (必要に応じて拡張)
    Q_UNUSED(message);
}

void ObsHttpWebSocketServer::broadcastToObs(const QString& actionType, const QJsonObject& payload) {
    QMutexLocker locker(&m_mutex);
    
    QJsonObject message;
    message["type"] = actionType;
    message["data"] = payload;
    
    QJsonDocument doc(message);
    QString jsonStr = doc.toJson(QJsonDocument::Compact);
    
    for (QWebSocket* client : m_clients) {
        client->sendTextMessage(jsonStr);
    }
}
