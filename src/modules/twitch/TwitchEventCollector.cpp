#include "TwitchEventCollector.h"
#include <algorithm>
#include <QCoreApplication>
#include <QDir>
#include <QDesktopServices>
#include <QUrlQuery>
#include <QJsonDocument>
#include <QJsonArray>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include "logger/Logger.h"
#include "core/twitch_credentials.h"

TwitchEventCollector::TwitchEventCollector()
    : m_workerThread(nullptr)
    , m_authServer(nullptr)
    , m_ircSocket(nullptr)
    , m_eventSubWebSocket(nullptr)
    , m_connected(false)
    , m_testTimer(nullptr)
    , m_testMessageCounter(0)
{
}

TwitchEventCollector::~TwitchEventCollector() {
    stopCollectorThread();
}

void TwitchEventCollector::startCollectorThread() {
    if (!m_workerThread) {
        m_workerThread = new QThread();
        this->moveToThread(m_workerThread);
        
        // テストモード時はタイマーをスレッド上で起動
        connect(m_workerThread, &QThread::started, this, [this]() {
#ifdef ENABLE_TEST_MODE
            m_testTimer = new QTimer(this);
            connect(m_testTimer, &QTimer::timeout, this, &TwitchEventCollector::onTestTimerTick);
#endif
        });
        
        m_workerThread->start();
    }
}

void TwitchEventCollector::stopCollectorThread() {
    if (m_workerThread) {
        QMetaObject::invokeMethod(this, "disconnectFromTwitch", Qt::BlockingQueuedConnection);
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
        m_workerThread = nullptr;
    } else {
        disconnectFromTwitch();
    }
}

void TwitchEventCollector::connectToTwitch(const QString& channelName, const QString& oauthToken) {
    m_channelName = channelName.toLower();
    m_oauthToken = oauthToken;
    
    Logger::instance().log("INFO", "TwitchEventCollector", "connectToTwitch", 
                           QString("Initiating connection for channel: %1").arg(m_channelName));

#ifdef ENABLE_TEST_MODE
    // テストモード時はネットワーク接続を行わず、スタブ接続をシミュレート
    m_connected = true;
    emit connectionStatusChanged(true, QString("%1 (TestMode)").arg(m_channelName));
    if (m_testTimer) {
        m_testTimer->start(4000); // 4秒ごとにダミーイベント発火
    }
    Logger::instance().log("INFO", "TwitchEventCollector", "connectToTwitch", "Connected in TEST MODE");
    return;
#endif

    if (m_oauthToken.isEmpty()) {
        // トークンが空の場合はブラウザを開いてOAuth認証を実行
        startAuthServer();
        
        QString authUrl = QString(
            "https://id.twitch.tv/oauth2/authorize"
            "?client_id=%1"
            "&redirect_uri=http://localhost:%2/"
            "&response_type=token"
            "&scope=chat:read+chat:edit+channel:read:redemptions"
        ).arg(TWITCH_CLIENT_ID).arg(TWITCH_REDIRECT_PORT);
        
        Logger::instance().log("INFO", "TwitchEventCollector", "connectToTwitch", "Opening browser for Twitch Authentication");
        QDesktopServices::openUrl(QUrl(authUrl));
    } else {
        // 既存トークンで接続
        connectIrc();
        connectEventSub();
    }
}

void TwitchEventCollector::disconnectFromTwitch() {
    Logger::instance().log("INFO", "TwitchEventCollector", "disconnectFromTwitch", "Disconnecting from Twitch");
    
    stopAuthServer();
    
    if (m_ircSocket) {
        m_ircSocket->close();
        m_ircSocket->deleteLater();
        m_ircSocket = nullptr;
    }
    
    if (m_eventSubWebSocket) {
        m_eventSubWebSocket->close();
        m_eventSubWebSocket->deleteLater();
        m_eventSubWebSocket = nullptr;
    }
    
    if (m_testTimer) {
        m_testTimer->stop();
    }
    
    m_connected = false;
    emit connectionStatusChanged(false, "");
}

void TwitchEventCollector::postChatMessage(const QString& message) {
    if (!m_connected) return;
    
#ifdef ENABLE_TEST_MODE
    Logger::instance().log("INFO", "TwitchEventCollector", "postChatMessage", 
                           QString("[MOCK CHAT OUT] %1").arg(message));
    return;
#endif

    if (m_ircSocket && m_ircSocket->isOpen()) {
        QString rawCmd = QString("PRIVMSG #%1 :%2\r\n").arg(m_channelName).arg(message);
        m_ircSocket->write(rawCmd.toUtf8());
        Logger::instance().log("INFO", "TwitchEventCollector", "postChatMessage", QString("Sent chat: %1").arg(message));
    }
}

// --- 臨時認証サーバーの実装 ---
void TwitchEventCollector::startAuthServer() {
    stopAuthServer();
    m_authServer = new QTcpServer(this);
    connect(m_authServer, &QTcpServer::newConnection, this, &TwitchEventCollector::onNewAuthConnection);
    
    if (m_authServer->listen(QHostAddress::LocalHost, TWITCH_REDIRECT_PORT)) {
        Logger::instance().log("INFO", "TwitchEventCollector", "startAuthServer", 
                               QString("Auth Server listening on port %1").arg(TWITCH_REDIRECT_PORT));
    } else {
        Logger::instance().log("ERROR", "TwitchEventCollector", "startAuthServer", "Failed to start Auth Server");
    }
}

void TwitchEventCollector::stopAuthServer() {
    if (m_authServer) {
        m_authServer->close();
        m_authServer->deleteLater();
        m_authServer = nullptr;
        Logger::instance().log("INFO", "TwitchEventCollector", "stopAuthServer", "Auth Server stopped");
    }
}

void TwitchEventCollector::onNewAuthConnection() {
    QTcpSocket* socket = m_authServer->nextPendingConnection();
    if (socket) {
        connect(socket, &QTcpSocket::readyRead, this, &TwitchEventCollector::onAuthReadyRead);
        connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
    }
}

void TwitchEventCollector::onAuthReadyRead() {
    QTcpSocket* socket = qobject_cast<QTcpSocket*>(sender());
    if (!socket) return;
    
    QByteArray data = socket->readAll();
    QString request = QString::fromUtf8(data);
    QStringList lines = request.split("\r\n");
    if (lines.isEmpty()) {
        socket->close();
        return;
    }
    
    QStringList tokens = lines[0].split(" ");
    if (tokens.size() < 2) {
        socket->close();
        return;
    }
    
    QString path = tokens[1];
    
    if (path == "/") {
        // ハッシュ値をクエリ文字列へ変換してリダイレクトする中間スクリプトを返す
        QByteArray response = 
            "HTTP/1.1 200 OK\r\n"
            "Content-Type: text/html; charset=utf-8\r\n"
            "Connection: close\r\n\r\n"
            "<html>"
            "<head><title>Redirecting...</title></head>"
            "<body>"
            "<p>Processing Twitch login... please wait.</p>"
            "<script>"
            "if (window.location.hash) {"
            "  window.location.href = '/token?' + window.location.hash.substring(1);"
            "} else {"
            "  document.body.innerHTML = '<h1>Authentication Error</h1><p>No hash fragment found.</p>';"
            "}"
            "</script>"
            "</body>"
            "</html>";
        socket->write(response);
        socket->disconnectFromHost();
    } else if (path.startsWith("/token")) {
        // トークンの抽出
        QUrl url(path);
        QUrlQuery query(url.query());
        QString token = query.queryItemValue("access_token");
        
        if (!token.isEmpty()) {
            QByteArray response = 
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html; charset=utf-8\r\n"
                "Connection: close\r\n\r\n"
                "<html>"
                "<head><title>Twitch Authentication Complete</title></head>"
                "<body>"
                "<h1>Authentication Successful!</h1>"
                "<p>Successfully linked with Twitch. You can now close this browser window and return to the application.</p>"
                "</body>"
                "</html>";
            socket->write(response);
            socket->disconnectFromHost();
            
            Logger::instance().log("INFO", "TwitchEventCollector", "onAuthReadyRead", "Twitch token retrieved successfully");
            m_oauthToken = token;
            emit tokenRetrieved(token);
            
            // 認証サーバーを閉じて、実際の接続処理へ
            QTimer::singleShot(1000, this, [this]() {
                stopAuthServer();
                connectIrc();
                connectEventSub();
            });
        } else {
            QByteArray response = "HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\nMissing token.";
            socket->write(response);
            socket->disconnectFromHost();
        }
    } else {
        socket->close();
    }
}

// --- IRCクライアントの実装 ---
void TwitchEventCollector::connectIrc() {
    if (m_ircSocket) {
        m_ircSocket->close();
        m_ircSocket->deleteLater();
    }
    
    m_ircSocket = new QSslSocket(this);
    connect(m_ircSocket, &QSslSocket::connected, this, &TwitchEventCollector::onIrcConnected);
    connect(m_ircSocket, &QSslSocket::readyRead, this, &TwitchEventCollector::onIrcReadyRead);
    connect(m_ircSocket, &QSslSocket::disconnected, this, &TwitchEventCollector::onIrcDisconnected);
    
    Logger::instance().log("INFO", "TwitchEventCollector", "connectIrc", "Connecting to Twitch IRC...");
    m_ircSocket->connectToHostEncrypted("irc.chat.twitch.tv", 443);
}

void TwitchEventCollector::onIrcConnected() {
    Logger::instance().log("INFO", "TwitchEventCollector", "onIrcConnected", "Twitch IRC Connected. Authenticating...");
    
    m_ircSocket->write(QString("PASS oauth:%1\r\n").arg(m_oauthToken).toUtf8());
    m_ircSocket->write(QString("NICK %1\r\n").arg(m_channelName).toUtf8());
    m_ircSocket->write(QString("JOIN #%1\r\n").arg(m_channelName).toUtf8());
    m_ircSocket->write("CAP REQ :twitch.tv/tags twitch.tv/commands\r\n"); // バッジ等の詳細情報を有効化
    
    m_connected = true;
    emit connectionStatusChanged(true, m_channelName);
}

void TwitchEventCollector::onIrcReadyRead() {
    while (m_ircSocket->canReadLine()) {
        QByteArray lineBytes = m_ircSocket->readLine();
        QString line = QString::fromUtf8(lineBytes).trimmed();
        
        // PING応答
        if (line.startsWith("PING")) {
            m_ircSocket->write("PONG :tmi.twitch.tv\r\n");
            continue;
        }
        
        parseIrcMessage(line);
    }
}

static QString replaceEmotesWithHtml(const QString& text, const QString& emotesTag) {
    if (emotesTag.isEmpty()) return text.toHtmlEscaped();
    
    struct EmoteReplacement {
        int start;
        int end;
        QString emoteId;
    };
    QList<EmoteReplacement> replacements;
    QStringList emoteSpecs = emotesTag.split('/');
    for (const QString& spec : emoteSpecs) {
        QStringList parts = spec.split(':');
        if (parts.size() < 2) continue;
        QString emoteId = parts[0];
        QStringList ranges = parts[1].split(',');
        for (const QString& range : ranges) {
            QStringList bounds = range.split('-');
            if (bounds.size() == 2) {
                EmoteReplacement rep;
                rep.start = bounds[0].toInt();
                rep.end = bounds[1].toInt();
                rep.emoteId = emoteId;
                if (rep.start >= 0 && rep.end >= rep.start && rep.end < text.length()) {
                    replacements.append(rep);
                }
            }
        }
    }
    
    std::sort(replacements.begin(), replacements.end(), [](const EmoteReplacement& a, const EmoteReplacement& b) {
        return a.start < b.start;
    });
    
    QString result;
    int lastIdx = 0;
    for (const auto& rep : replacements) {
        if (rep.start < lastIdx) continue;
        if (rep.start > lastIdx) {
            result += text.mid(lastIdx, rep.start - lastIdx).toHtmlEscaped();
        }
        int len = rep.end - rep.start + 1;
        QString emoteText = text.mid(rep.start, len);
        QString imgHtml = QString("<img src=\"https://static-cdn.jtvnw.net/emoticons/v2/%1/default/dark/1.0\" alt=\"%2\" title=\"%2\" width=\"28\" height=\"28\" style=\"vertical-align: middle;\" />")
            .arg(rep.emoteId, emoteText.toHtmlEscaped());
        result += imgHtml;
        lastIdx = rep.end + 1;
    }
    if (lastIdx < text.length()) {
        result += text.mid(lastIdx).toHtmlEscaped();
    }
    return result;
}

void TwitchEventCollector::parseIrcMessage(const QString& rawMessage) {
    // 例: @badge-info=;badges=moderator/1;color=#1E90FF;display-name=Tester;emotes=;first-msg=0;mod=1;room-id=12345;subscriber=0;tmi-sent-ts=1625621400;turbo=0;user-id=98765;user-type=mod :tester!tester@tester.tmi.twitch.tv PRIVMSG #streamer :hello world
    if (rawMessage.contains("PRIVMSG")) {
        QStringList parts = rawMessage.split(" PRIVMSG #");
        if (parts.size() < 2) return;
        
        QString header = parts[0];
        QString contentAndChannel = parts[1];
        
        int firstColonIdx = contentAndChannel.indexOf(':');
        if (firstColonIdx == -1) return;
        
        QString commentText = contentAndChannel.mid(firstColonIdx + 1);
        
        // タグ解析
        QString displayName = "Anonymous";
        QString userId = "";
        QString username = "";
        QString emotesVal = "";
        QJsonArray badgesArray;
        
        // メッセージ送信者のニックネーム抽出（スタンプタグ等の値に含まれるコロンを避けるための抽出設計）
        int nickStart = -1;
        if (header.startsWith('@')) {
            int prefixStart = header.indexOf(" :");
            if (prefixStart != -1) {
                nickStart = prefixStart + 2; // " :"の次（ニックネームの開始位置）
            }
        } else if (header.startsWith(':')) {
            nickStart = 1; // 先頭のコロンの次
        }
        
        if (nickStart != -1) {
            int nickEnd = header.indexOf('!', nickStart);
            if (nickEnd != -1 && nickEnd > nickStart) {
                username = header.mid(nickStart, nickEnd - nickStart);
                displayName = username; // フォールバック
            }
        }
        
        if (header.startsWith('@')) {
            QString tagsPart = header.mid(1, header.indexOf(' ') - 1);
            QStringList tags = tagsPart.split(';');
            for (const QString& tag : tags) {
                QStringList kv = tag.split('=');
                if (kv.size() == 2) {
                    QString key = kv[0];
                    QString val = kv[1];
                    if (key == "display-name") {
                        displayName = val.isEmpty() ? username : val;
                    } else if (key == "user-id") {
                        userId = val;
                    } else if (key == "emotes") {
                        emotesVal = val;
                    } else if (key == "badges") {
                        // バッジ情報の簡易抽出
                        QStringList badgeList = val.split(',');
                        for (const QString& badge : badgeList) {
                            QJsonObject bObj;
                            bObj["name"] = badge;
                            badgesArray.append(bObj);
                        }
                    }
                }
            }
        }
        
        TwitchComment comment;
        comment.id = QUuid::createUuid().toString();
        comment.userId = userId;
        comment.username = username;
        comment.displayName = displayName;
        comment.comment = replaceEmotesWithHtml(commentText, emotesVal);
        comment.badges = badgesArray;
        comment.timestamp = QDateTime::currentMSecsSinceEpoch();
        
        emit commentReceived(comment);
    }
}

void TwitchEventCollector::onIrcDisconnected() {
    Logger::instance().log("WARNING", "TwitchEventCollector", "onIrcDisconnected", "Twitch IRC Disconnected");
    m_connected = false;
    emit connectionStatusChanged(false, "");
}

// --- EventSub クライアントの実装 (接続スケルトン) ---
void TwitchEventCollector::connectEventSub() {
    if (m_eventSubWebSocket) {
        m_eventSubWebSocket->close();
        m_eventSubWebSocket->deleteLater();
    }
    
    m_eventSubWebSocket = new QWebSocket();
    connect(m_eventSubWebSocket, &QWebSocket::connected, this, &TwitchEventCollector::onEventSubConnected);
    connect(m_eventSubWebSocket, &QWebSocket::textMessageReceived, this, &TwitchEventCollector::onEventSubTextMessageReceived);
    connect(m_eventSubWebSocket, &QWebSocket::disconnected, this, &TwitchEventCollector::onEventSubDisconnected);
    
    Logger::instance().log("INFO", "TwitchEventCollector", "connectEventSub", "Connecting to Twitch EventSub WebSocket...");
    m_eventSubWebSocket->open(QUrl("wss://eventsub.wss.twitch.tv/v1/welcome"));
}

void TwitchEventCollector::onEventSubConnected() {
    Logger::instance().log("INFO", "TwitchEventCollector", "onEventSubConnected", "Twitch EventSub WebSocket Connected.");
}

void TwitchEventCollector::onEventSubTextMessageReceived(const QString& message) {
    // Twitch EventSub からのメッセージ受信処理
    // 必要に応じて、チャネルポイント等のイベント受信をパース
    Q_UNUSED(message);
}

void TwitchEventCollector::onEventSubDisconnected() {
    Logger::instance().log("WARNING", "TwitchEventCollector", "onEventSubDisconnected", "Twitch EventSub WebSocket Disconnected");
}

// --- MOCK / TEST MODE 用イベント発火 ---
void TwitchEventCollector::onTestTimerTick() {
    m_testMessageCounter++;
    
    if (m_testMessageCounter % 3 != 0) {
        // コメントイベントのテスト発火
        TwitchComment comment;
        comment.id = QString("mock-msg-%1").arg(m_testMessageCounter);
        comment.userId = "mock-user-123";
        comment.username = "viewer_bot";
        comment.displayName = "常連リスナー";
        
        if (m_testMessageCounter % 4 == 0) {
            comment.comment = "こんにちは！お疲れ様です！";
        } else if (m_testMessageCounter % 4 == 1) {
            comment.comment = "VOICEVOXのずんだもんの声テストなのだ";
        } else if (m_testMessageCounter % 4 == 2) {
            comment.comment = "時報プラグインのテストをしてほしいのだ";
        } else {
            comment.comment = "コメント読み上げはいい文明";
        }
        
        QJsonObject badgeObj;
        badgeObj["name"] = "subscriber/12";
        comment.badges.append(badgeObj);
        comment.timestamp = QDateTime::currentMSecsSinceEpoch();
        
        emit commentReceived(comment);
    } else {
        // チャンネルポイント引き換えイベントのテスト発火
        TwitchRewardRedemption redemption;
        redemption.id = QString("mock-redemp-%1").arg(m_testMessageCounter);
        redemption.rewardId = "reward-sound-alert";
        redemption.rewardName = "音声を鳴らす";
        redemption.userId = "mock-user-456";
        redemption.username = "points_hoarder";
        redemption.displayName = "ポイント富豪";
        redemption.userInput = "わーい！";
        redemption.timestamp = QDateTime::currentMSecsSinceEpoch();
        
        emit rewardRedeemed(redemption);
    }
}

QList<TwitchRewardInfo> TwitchEventCollector::getChannelPointRewards() {
    Logger::instance().log("INFO", "TwitchEventCollector", "getChannelPointRewards", "Fetching channel point rewards from Twitch Helix API...");

    if (m_oauthToken.isEmpty()) {
        Logger::instance().log("WARNING", "TwitchEventCollector", "getChannelPointRewards", "OAuth token is empty. Returning mock/fallback list.");
        TwitchRewardInfo mock1{"reward-sound-alert", "音声を鳴らす", 100, true};
        TwitchRewardInfo mock2{"reward-highlight", "コメント強調", 50, true};
        return {mock1, mock2};
    }

    QNetworkAccessManager nam;
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

    // 1. Broadcaster ID の取得 (Users API)
    QString broadcasterId = "";
    QUrl userUrl("https://api.twitch.tv/helix/users");
    if (!m_channelName.isEmpty()) {
        QUrlQuery query;
        query.addQueryItem("login", m_channelName);
        userUrl.setQuery(query);
    }

    QNetworkRequest userReq(userUrl);
    userReq.setRawHeader("Authorization", QString("Bearer %1").arg(m_oauthToken).toUtf8());
    userReq.setRawHeader("Client-Id", TWITCH_CLIENT_ID.toUtf8());

    QNetworkReply* userReply = nam.get(userReq);
    connect(userReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(5000); // 5秒タイムアウト
    loop.exec();

    if (userReply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(userReply->readAll());
        QJsonObject obj = doc.object();
        QJsonArray data = obj.value("data").toArray();
        if (!data.isEmpty()) {
            broadcasterId = data.at(0).toObject().value("id").toString();
        }
    }
    userReply->deleteLater();

    if (broadcasterId.isEmpty()) {
        Logger::instance().log("ERROR", "TwitchEventCollector", "getChannelPointRewards", "Failed to retrieve broadcaster ID");
        return {};
    }

    // 2. チャンネルポイント報酬一覧の取得 (custom_rewards API)
    QUrl rewardUrl(QString("https://api.twitch.tv/helix/channel_points/custom_rewards?broadcaster_id=%1").arg(broadcasterId));
    QNetworkRequest rewardReq(rewardUrl);
    rewardReq.setRawHeader("Authorization", QString("Bearer %1").arg(m_oauthToken).toUtf8());
    rewardReq.setRawHeader("Client-Id", TWITCH_CLIENT_ID.toUtf8());

    QNetworkReply* rewardReply = nam.get(rewardReq);
    connect(rewardReply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    timer.start(5000); // 5秒タイムアウト
    loop.exec();

    QList<TwitchRewardInfo> rewardsList;
    if (rewardReply->error() == QNetworkReply::NoError) {
        QJsonDocument doc = QJsonDocument::fromJson(rewardReply->readAll());
        QJsonObject obj = doc.object();
        QJsonArray data = obj.value("data").toArray();
        for (const QJsonValue& val : data) {
            QJsonObject item = val.toObject();
            TwitchRewardInfo info;
            info.id = item.value("id").toString();
            info.title = item.value("title").toString();
            info.cost = item.value("cost").toInt();
            info.isEnabled = item.value("is_enabled").toBool();
            rewardsList.append(info);
        }
        Logger::instance().log("INFO", "TwitchEventCollector", "getChannelPointRewards", QString("Successfully retrieved %1 rewards").arg(rewardsList.size()));
    } else {
        Logger::instance().log("ERROR", "TwitchEventCollector", "getChannelPointRewards", QString("Failed to fetch custom rewards: %1").arg(rewardReply->errorString()));
    }
    rewardReply->deleteLater();

    return rewardsList;
}
