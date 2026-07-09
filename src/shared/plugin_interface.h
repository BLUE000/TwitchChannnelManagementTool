#pragma once
#include <QtPlugin>
#include <QWidget>
#include <QJsonObject>
#include <QJsonArray>
#include <QMap>
#include <QByteArray>
#include <QString>

// --- データ構造定義 ---
struct TwitchComment {
    QString id;
    QString userId;
    QString username;
    QString displayName;
    QString comment;
    QString avatarUrl;
    QJsonArray badges;
    qint64 timestamp;
};

struct TwitchRewardRedemption {
    QString id;
    QString rewardId;
    QString rewardName;
    QString userId;
    QString username;
    QString displayName;
    QString userInput;
    qint64 timestamp;
};

// --- コアコンテキスト（プラグインからコアへのAPI） ---
class ICoreContext {
public:
    virtual ~ICoreContext() = default;
    
    // コアへの直接要求 (非同期)
    virtual void sendChatMessage(const QString& message) = 0;
    virtual void requestTts(const QString& text, const QString& speakerId, int speed, int pitch, int volume) = 0;
    virtual void sendToObs(const QString& action, const QJsonObject& payload) = 0;
    virtual void postDiscordWebhook(const QString& webhookUrl, const QJsonObject& payload) = 0; // Discord Webhook送信代行
    
    // パス・セキュリティキー取得
    virtual QString getPluginDirectory() const = 0;
    virtual QString getCipherKey() const = 0;

    // 暗号化ファイルI/O (透過的なTransCipher保護、新規追加)
    virtual bool writeEncryptedFile(const QString& relativePath, const QByteArray& data) = 0;
    virtual QByteArray readEncryptedFile(const QString& relativePath) = 0;
};

// --- プラグインインターフェース ---
class IChannelPlugin {
public:
    virtual ~IChannelPlugin() = default;
    
    // 初期化 & 破棄
    virtual void initialize(ICoreContext* context) = 0;
    virtual void shutdown() = 0;
    
    // プラグイン情報
    virtual QString pluginId() const = 0;
    virtual QString pluginName() const = 0;
    virtual QString pluginVersion() const = 0;
    virtual QString pluginDescription() const = 0; // プラグイン詳細説明
    virtual QByteArray iconPngData() const = 0;   // タブ用アイコン画像 (PNGバイナリデータ)
    virtual QMap<QString, QByteArray> defaultAssets() const = 0; // 内蔵デフォルトアセット
    
    // GUIの提供（UIを持たないバックグラウンドプラグインの場合は nullptr を返す）
    virtual QWidget* createWidget(QWidget* parent = nullptr) = 0;
    
    // 下りシグナルのハンドリング用メソッド
    virtual void onCommentReceived(const TwitchComment& comment) = 0;
    virtual void onRewardRedeemed(const TwitchRewardRedemption& redemption) = 0;
    virtual void onTick() = 0; // 1秒ごとのタイマーイベント
};

#define IChannelPlugin_iid "com.blue000.twitchchannelmanagementtool.IChannelPlugin"
Q_DECLARE_INTERFACE(IChannelPlugin, IChannelPlugin_iid)
