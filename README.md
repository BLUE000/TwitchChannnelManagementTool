# TwitchChannelManagementTool

Twitchの配信管理、チャット読み上げ（棒読みちゃん/VOICEVOX）、OBS Studioとの連携オーバーレイ機能、およびプラグインシステムを提供する、モジュール型チャンネル管理ツールです。

---

## 概要 (Overview)
本ツールは、コア機能（Twitch接続、TTSエンジン管理、OBS WebSocket/HTTPサーバー、暗号化ロガー）を内蔵し、動的ライブラリ（DLL）形式で作成された任意のプラグインをロードして機能を自在に拡張できるプラットフォームです。

また、セキュリティと出自証明の保護のため、以下のコンポーネントと統合されています。
*   **TransCipher**: 機密性の高い設定ファイル（`settings.bin`）およびシステムログを暗号化・保護。
*   **TrustChain**: ローカルとリモート（origin）間のGitツリーの差異を検知し、非公式ビルドの自動出自表示（ウォーターマーク）を強制。
*   **BinMarkManager**: 実行可能ファイル（EXE）に平文コピーライトおよび透かしメタデータを安全に埋め込み。

---

## プラグイン向け API 仕様 (Plugin API Specification)

本ツールは、Qt 6 に基づく動的プラグインシステムを採用しています。プラグインを作成する際は、`src/shared/plugin_interface.h` に定義されたインターフェースを実装します。

### 1. データ構造定義 (Data Structures)

#### TwitchComment
Twitchから受信したチャットコメントを表す構造体。
```cpp
struct TwitchComment {
    QString id;           // メッセージのユニークID
    QString userId;       // ユーザーの Twitch ID
    QString username;     // アカウント名（英数字）
    QString displayName;  // 表示名（日本語等のローカライズ名）
    QString comment;      // チャット本文
    QString avatarUrl;    // プロフィールアイコンURL
    QJsonArray badges;    // バッジ一覧
    qint64 timestamp;     // 受信エポック秒
};
```

#### TwitchRewardRedemption
Twitchチャンネルポイント報酬の引き換え（Redeem）イベントを表す構造体。
```cpp
struct TwitchRewardRedemption {
    QString id;           // 引き換えイベントのユニークID
    QString rewardId;     // 報酬項目自体のID
    QString rewardName;   // 報酬名
    QString userId;       // 引き換えたユーザーの Twitch ID
    QString username;     // アカウント名（英数字）
    QString displayName;  // 表示名
    QString userInput;    // 引き換え時に入力したテキスト（ある場合のみ）
    qint64 timestamp;     // 引き換えエポック秒
};
```

---

### 2. ICoreContext（コアからプラグインへ提供する機能）
プラグインの初期化時に渡され、本体コアのシステムサービスを呼び出すためのAPIです。

| メソッド名 | 説明 |
| :--- | :--- |
| `void sendChatMessage(const QString& message)` | Twitchチャットにテキストを送信します。 |
| `void requestTts(const QString& text, const QString& speakerId, int speed, int pitch, int volume)` | 指定した音声合成ツールで読み上げをリクエストします。 |
| `void sendToObs(const QString& action, const QJsonObject& payload)` | OBS向けWebSocketサーバー経由で、ブラウザソース（JavaScript）へJSONデータを配信します。 |
| `void postDiscordWebhook(const QString& webhookUrl, const QJsonObject& payload)` | 指定したDiscord Webhook URLへ、JSONペイロードを非同期で送信します（安全なスレッド処理をコアが代行）。 |
| `QString getPluginDirectory() const` | プラグイン固有のプライベートデータ保存用ディレクトリパスを取得します。 |
| `QString getCipherKey() const` | ログやデータの暗号化・難読化に利用する共通シークレットキーを取得します。 |
| `bool writeEncryptedFile(const QString& relativePath, const QByteArray& data)` | 平文データと相対ファイル名を受け取り、TransCipherで暗号化してファイルへ新規上書き保存します。 |
| `QByteArray readEncryptedFile(const QString& relativePath)` | 暗号化されたファイルを読み込み、TransCipherで復号した平文のデータを返却します（ファイル不在時は空データを返却）。 |
| `void writeLog(const QString& level, const QString& className, const QString& funcName, const QString& description)` | ホスト側のロギング機能（TransCipherでの暗号化）を利用して、システムログにログエントリを記録します。 |

---

### 3. IChannelPlugin（プラグインが実装すべきインターフェース）
すべてのプラグインはこのクラスを継承し、以下の仮想関数を実装する必要があります。

#### ライフサイクル
```cpp
virtual void initialize(ICoreContext* context) = 0;
```
プラグインの読み込み時に一度呼び出されます。`ICoreContext` のポインタを受け取り、メンバ変数等に保持してください。
```cpp
virtual void shutdown() = 0;
```
アプリケーション終了時、またはプラグインのアンロード時に呼び出されます。スレッドの停止やリソースの解放を行ってください。

#### プラグイン基本情報
*   `virtual QString pluginId() const = 0;` : プラグインの一意識別子（例：`"com.example.chatlogger"`）。
*   `virtual QString pluginName() const = 0;` : 設定タブやサイドバーに表示される名前。
*   `virtual QString pluginVersion() const = 0;` : バージョン文字列（例：`"1.0.0"`）。
*   `virtual QString pluginDescription() const = 0;` : プラグインの機能説明。
*   `virtual QByteArray iconPngData() const = 0;` : 左サイドバーのタブ画像に使用されるアイコンのPNGバイナリデータ（48x48 of transparent PNG recommended）。
*   `virtual QMap<QString, QByteArray> defaultAssets() const = 0;` : OBS配信用静的ファイルアセット（HTML/CSS/JS等）の名前とデータのマップ。

#### UIの提供
```cpp
virtual QWidget* createWidget(QWidget* parent = nullptr) = 0;
```
メインウィンドウの右側エリアに表示される設定/表示用UIウィジェットを作成して返します。UIを持たないバックグラウンドプラグインの場合は `nullptr` を返してください。

#### 下りイベントシグナル
*   `virtual void onCommentReceived(const TwitchComment& comment) = 0;` : 新しいチャットコメントを受信したときに呼び出されます。
*   `virtual void onRewardRedeemed(const TwitchRewardRedemption& redemption) = 0;` : チャンネルポイント報酬が引き換えられたときに呼び出されます。
*   `virtual void onTick() = 0;` : 1秒周期で自動的に呼び出されるタイマーハンドラ。時間経過に伴う処理に利用できます。

#### メタデータマクロ定義
プラグインソースファイルの末尾等に、Qtのプラグイン公開用マクロを記述してください。
```cpp
#define IChannelPlugin_iid "com.blue000.twitchchannelmanagementtool.IChannelPlugin"
Q_DECLARE_INTERFACE(IChannelPlugin, IChannelPlugin_iid)
```

---

### 4. プラグイン実装時の注意点・フリーズ防止ガイドライン (Plugin Implementation Guidelines)

プラグインを開発する際は、ホストアプリケーション（メインGUIスレッド）の応答性維持および動作安定性のため、以下の点に十分注意して実装してください。

#### ① プラグインファイル（DLL）の重複・競合の回避
- `plugins/` フォルダ内に同一プラグインの旧バージョンや別名DLL（例: `ChannelPointPlugin.dll`（旧）と `libChannelPointPlugin.dll`（新））が同時に存在すると、ホストアプリが二重ロードを行い、初期化衝突やリソース競合・フリーズを引き起こす可能性があります。
- ビルド出力名や配布ファイル名は統一し、古いDLLファイルが残留しないよう構成・クリーンアップしてください。

#### ② 初期化メソッド内での同期通信・ブロッキング処理の禁止
- `initialize(ICoreContext* context)` や `createWidget(QWidget* parent)` は、ホストアプリケーションのメインGUIスレッド上で同期実行されます。
- これらのメソッド内でネットワーク通信（OBS/Twitch/HTTP等）の同期接続待ち（タイムアウトなしのブロック）を実行すると、Qtのメインイベントループが停止し、アプリ全体が「応答なし（フリーズ）」になります。
- ネットワーク通信や重い処理は、`QThread`（ワーカースレッド）への退避や `QTimer::singleShot` 等を用いた非同期処理に委譲してください。

#### ③ プラグイン初期化時のディスク I/O 待ちの最小化
- `iconPngData()` やアセット取得処理内で毎回ファイル読み込み (`QFile`) を同期実行すると、ディスクアクセス待ちによりメインスレッドに描画遅延が発生する場合があります。
- アイコン画像や内蔵アセットは、Qtリソースシステム（`.qrc`）や static なメモリ上データとして静的キャッシュ化することを推奨します。

---

## ライセンス・著作権表記 (Licensing & Copyrights)

### TwitchChannelManagementTool
```text
MIT License

Copyright (c) 2026 BLUE

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
```

---

### TrustChain
```text
This software uses TrustChain Module.
Copyright (c) 2026 BLUE000.
```

---

### TransCipher
```text
This software uses TransCipher library.
Copyright (c) 2026 BLUE000.
```

---

### BinMarkManager
```text
This software uses BinMarkManager library.
Copyright (c) 2026 BLUE000.
```

---

### Qt 6 (サードパーティ・ライセンス)
```text
Qt is licensed under the GNU Lesser General Public License (LGPL) version 3.
Copyright (C) 2024 The Qt Company Ltd and other contributors.
```
