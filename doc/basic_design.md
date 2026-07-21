# TwitchChannelManagementTool 基本設計書 (ドラフト)

本ドキュメントは、TwitchChannelManagementToolのアプリケーション本体コアにおける基本設計（システムアーキテクチャ、インターフェース、データ設計、UI設計）を定義するものです。

---

## 1. システムアーキテクチャ設計

コアアプリケーションは、UI（表示層）、コア・コントローラ（制御層）、および外部通信モジュール（サービス層）に分離し、さらに動的ロードされたプラグインを制御する「プラグイン駆動アーキテクチャ」を採用します。

```mermaid
graph TD
    subgraph UI層 [UI層 (GUI Thread)]
        MainWindow["MainWindow (縦タブUI)"]
        SettingsTab["共通設定画面 (プラグイン管理)"]
    end

    subgraph コントローラ層 [コントローラ層 (GUI Thread)]
        AppController["AppController (コア制御)"]
        PluginLoader["PluginLoader (DLL動的ロード)"]
        SignalDispatcher["SignalDispatcher (下りシグナル仲介)"]
    end

    subgraph サービス層 [サービス層 (スレッド分離 / 独立モジュール)]
        TwitchCollector["TwitchEventCollector (EventSub / IRC)"]
        ObsServer["ObsHttpWebSocketServer (HTTP / WS)"]
        TtsManager["TtsManager (音声合成連携窓口)"]
        LoggerModule["LoggerModule (難読化ログ出力)"]
    end

    subgraph プラグイン層 [プラグイン層 (DLL)]
        PluginA["Plugin A (DLL)"]
        PluginB["Plugin B (DLL)"]
    end

    %% 下り方向 (シグナル)
    MainWindow == 1.操作 ==> AppController
    AppController -- 2.ディスパッチ --> SignalDispatcher
    SignalDispatcher -- 3.Signal --> PluginA
    SignalDispatcher -- 3.Signal --> PluginB
    TwitchCollector -. 2.受信通知 .-> AppController

    %% 上り方向 (イベント)
    PluginA == 1.Event (postEvent) ==> AppController
    PluginB == 1.Event (postEvent) ==> AppController
    AppController -- 2.通知 --> MainWindow
    AppController -- 3.要求 --> TtsManager
    AppController -- 3.要求 --> ObsServer

    %% ログ出力関係
    AppController -. ログ記録 .-> LoggerModule
    TwitchCollector -. ログ記録 .-> LoggerModule
    ObsServer -. ログ記録 .-> LoggerModule
    TtsManager -. ログ記録 .-> LoggerModule

```

### 1.1. モジュール構成・ディレクトリ構造
本プロジェクトは、機能ごとに明確にディレクトリを分割して各コンポーネントの責任範囲を分離します。

```
TwitchChannelManagementTool/
├── doc/                      # 要件定義・基本/詳細設計書
├── lib/                      # 依存ライブラリ (Gitサブモジュール)
│   ├── TrustChain/           # ビルド時検証・署名モジュール
│   └── TransCipher-Dist/     # 暗号化・難読化モジュール
├── src/                      # アプリケーションソースコード
│   ├── main.cpp              # エントリーポイント
│   ├── core/                 # アプリケーション制御・管理層
│   │   ├── AppController     # メインロジックコントローラ
│   │   ├── PluginLoader      # QPluginLoaderによるDLLロード制御
│   │   └── SignalDispatcher  # 下りシグナルの仲介役
│   ├── modules/              # 各独立機能モジュール（サービス層）
│   │   ├── logger/           # 難読化ログ出力モジュール
│   │   ├── twitch/           # Twitch接続・Helix API・EventSubクライアント
│   │   ├── obs/              # OBS連携用HTTP/WebSocketサーバー
│   │   └── tts/              # 音声合成ツール連携モジュール
│   │       ├── ITtsClient.h  # 共通抽象インターフェース
│   │       ├── BouyomiClient # 棒読みちゃん用固有実装 (TCP)
│   │       ├── VoiceVoxClient # VOICEVOX用固有実装 (REST API)
│   │       └── TtsManager    # 動的切替・スレッド実行制御
│   ├── ui/                   # ユーザーインターフェース（GUI画面層）
│   │   ├── MainWindow        # メインウィンドウ (縦タブUI)
│   │   └── SettingsTab       # 共通設定画面 (プラグイン一覧表示を含む)
│   └── shared/               # プラグイン・本体の共通インターフェース
│       └── plugin_interface.h # 共通構造体および抽象クラス定義
├── logs/                     # 実行時ログ保存用フォルダ (Git管理外)
└── CMakeLists.txt            # ビルドスクリプト定義
```

### 1.2. スレッド分離モデル (Thread Isolation)
GUIのフリーズを防ぐため、ソケット通信やI/Oを伴うサービス層はそれぞれバックグラウンドスレッド（`QThread`）上で稼働させます。
* `TwitchEventCollector` (WebSocket / RESTスレッド)
* `ObsHttpWebSocketServer` (HTTP / WebSocketサーバースレッド)
* `TtsManager` (音声合成連携制御スレッド)

---

## 2. 通信プロトコル＆フロー設計

### 2.1. 下り方向の通信（シグナル方式）
* **フロー**: `画面 (UI)` ➔ `コア` ➔ `各モジュール` ➔ `プラグイン`
* **設計**:
  * コア内に `SignalDispatcher` を定義し、画面操作やTwitchイベントのシグナルを集約します。
  * 各プラグインの読み込み時に、`SignalDispatcher` の各シグナルとプラグインのハンドラ（スロット）を `Qt::QueuedConnection`（スレッド間安全な非同期接続）で自動的に接続します。

### 2.2. 上り方向の通信（イベント方式）
* **フロー**: `プラグイン` ➔ `各モジュール` ➔ `コア` ➔ `画面`
* **設計**:
  * コア側（`AppController` / `MainWindow`）に `onNotifyEvents(QEvent* event)` という統合イベント受信ハンドラを実装します。
  * プラグインが本体に対して処理要求（読み上げ要求、チャット送信、OBS表示等）を行う際は、カスタムイベントクラス `PluginNotifyEvent` をインスタンス化し、`QCoreApplication::postEvent()` を用いて非同期でスレッドセーフにコアにイベントを送信します。

---

## 3. プラグイン・インターフェース設計

各プラグインは以下のC++インターフェース（`IChannelPlugin`）を実装したDLLとして作成されます。

```cpp
#pragma once
#include <QtPlugin>
#include <QWidget>
#include <QJsonObject>
#include <QMap>

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

struct TwitchRewardInfo {
    QString id;          // 報酬ID
    QString title;       // 報酬名
    int cost;            // 消費ポイント数
    bool isEnabled;      // 有効状態
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
    
    // Twitch チャンネルポイント報酬一覧取得 (同期)
    virtual QList<TwitchRewardInfo> getChannelPointRewards() = 0;

    // パス・セキュリティキー取得
    virtual QString getPluginDirectory() const = 0;
    virtual QString getCipherKey() const = 0;

    // 暗号化ファイルI/O (透過的なTransCipher保護、新規追加)
    virtual bool writeEncryptedFile(const QString& relativePath, const QByteArray& data) = 0;
    virtual QByteArray readEncryptedFile(const QString& relativePath) = 0;

    // 暗号化システムログ出力 (新規追加)
    virtual void writeLog(const QString& level, const QString& className, const QString& funcName, const QString& description) = 0;
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
    
    // 下りシグナルのハンドリング用スロットメソッド
    virtual void onCommentReceived(const TwitchComment& comment) = 0;
    virtual void onRewardRedeemed(const TwitchRewardRedemption& redemption) = 0;
    virtual void onTick() = 0; // 1秒ごとのタイマーイベント
};

#define IChannelPlugin_iid "com.blue000.twitchchannelmanagementtool.IChannelPlugin"
Q_DECLARE_INTERFACE(IChannelPlugin, IChannelPlugin_iid)
Q_DECLARE_METATYPE(TwitchRewardInfo)
```

### 3.1. プラグイン実装時の注意点・フリーズ防止ガイドライン
1. **プラグインファイル（DLL）の重複・競合の回避**: `plugins/` フォルダ内に同一プラグインの旧名DLL（例: `ChannelPointPlugin.dll` と `libChannelPointPlugin.dll`）が混在しないよう管理・クリーンアップすること。
2. **初期化メソッド内での同期ブロッキング処理の禁止**: `initialize()` や `createWidget()` はGUIスレッド上で同期実行されるため、タイムアウトなしの同期通信や重い処理を記述しないこと（`QThread` や非同期タイマーへ委譲すること）。
3. **ディスク I/O 待ちの最小化**: `iconPngData()` やアセット取得時の `QFile` 読み込みはメモリキャッシュ（.qrcリソース等）を利用し、GUIスレッドの遅延を防ぐこと。

### 3.2. ホストアプリ側プラグイン制御・互換性仕様
1. **QPluginLoader::unload() の絶対禁止（デッドロック防止）**:
   - Qtの仕様上、`QSqlDatabase` (SQLiteドライバー) や静的オブジェクトを含むDLLを `QPluginLoader::unload()` で途中で解除すると、Qt内部の静的クリーンアップ処理によりメインスレッドでデッドロックが確実に発生します。
   - ホストアプリ（`SettingsTab` および `PluginLoader`）は設定画面でのメタデータ取得時やアンロード時であっても `QPluginLoader::unload()` を直接呼び出さず、プロセスの終了までメモリ上に安全に維持します。
2. **DLLファイル名（`lib` プレフィックス）の柔軟マッピング**:
   - ビルド環境の違いによるファイル名変更（例: `CommentManagerPlugin.dll` ⇆ `libCommentManagerPlugin.dll`）が発生した場合でも設定データ（`settings.bin`）との互換性を保つため、ホストアプリは `lib` プレフィックスの有無の違いを吸収して自動的に同一プラグインとして認識・ロードします。
3. **OBS HTTP サーバーパケット分割（パケットバッファリング）制御**:
   - ブラウザやOBSブラウザソースからの HTTP GET リクエスト受診時、TCPパケット分割や遅延によりヘッダーが分割されて届いた際に、不完全なデータでソケットが即時切断（`ERR_EMPTY_RESPONSE`）されるのを防ぎます。
   - `onTcpReadyRead()` で `socket->peek()` を用いて HTTP ヘッダー終端（`\r\n\r\n` または `\n\n`）の受信完了を確認するまでソケットバッファを読み出さずに安全に保持します。

---

## 4. データ保存・暗号化（TransCipher）設計

プラグインのデータ保護は、本体コア（ホストアプリ）が提供する `ICoreContext` のインターフェースを通じて行います。プラグイン側は暗号化キーをハンドリングせず、平文データと相対ファイル名のみを指定します。

### 4.1. 暗号化保存プロセスフロー (保存時)
1. プラグインは自身のデータを JSON（`QJsonObject`）等のバイト配列（`QByteArray`）として平文のまま構築する。
2. プラグインは保存先相対パス（例: `"config.json"`）とデータを指定し、`ICoreContext::writeEncryptedFile(relativePath, plainData)` を呼び出す。
3. 本体コアは、受け取ったパスの安全性を検証（`..` によるディレクトリトラバーサル防止）の上、保存先の絶対パス（`getPluginDirectory() / relativePath`）を決定する。
4. 本体コア内部で、TransCipher 方式（共通難読化キー）によりデータを暗号化する。
5. 暗号化したデータ（Base64形式等）を、対象ファイルへ上書き保存（Write, Truncate）する。

### 4.2. 復号読み込みプロセスフロー (読み込み時)
1. プラグインは `ICoreContext::readEncryptedFile(relativePath)` を呼び出す。
2. 本体コアは、パスの安全性を検証の上、対象ファイルから暗号化データを読み込む。
3. 本体コア内部で、暗号化キーを用いて TransCipher による復号処理を実行する。
4. 復号された生のバイト配列（平文データ）をプラグインへ返却する。（ファイルが存在しない、または復号失敗時は空の `QByteArray()` が返される）。
5. プラグインは返却された平文のバイト配列を復元し、メモリ上に展開する。

## 5. ログ出力基本設計 (本体コア・独立モジュール)

本体コアおよび各サービス・UIは、セキュリティおよび動作検証のためのシステムログを、独立した共通の `Logger` モジュール（`src/modules/logger/`）を介してファイル出力します。

### 5.1. ログ形式 (フォーマット)
`[YYYY-MM][DD-HH-mm-ss][Action][Class][Func][Description]`

### 5.2. 難読化と追記保存
- 各ログ出力の実行時、1行ごとに上記フォーマットの文字列を構築し、本体に定義された共通の難読化キーを用いて `TransCipher` で難読化（`CipherEngine::encrypt`）します。
- 難読化されたデータは Base64 文字列として、ログファイルに1行ずつ追記（Append）保存されます（これにより、1行ごとの非同期デコードが可能）。

### 5.3. ログファイルの分割
- ログファイルは月ごとに分割し、ファイル名を `YYYY-MM.log`（例：`2026-07.log`）として `logs/` ディレクトリに保存します。

---

## 6. プラグイン・アセット配信基本設計 (OBS連携用)

プラグインがOBSなどの配信ソフトに表示するブラウザソース用HTMLオーバーレイなどのアセットは、本体コアが動作する静的HTTPサーバーから一括配信されます。

### 6.1. ディレクトリ構成
プラグインがロードされると、そのデフォルトアセットが `assets/` フォルダ配下にプラグイン名を用いたディレクトリ名で配置・展開されます。

```
assets/
└── overlay/
    └── [プラグイン名]/
        ├── default/     # プラグインから展開されたデフォルトアセット（コアが書き出す領域）
        └── custom/      # ユーザーが編集・追加するカスタムアセット（コアは変更しない領域）
```

### 6.2. 展開および上書き制限ルール
- **自動展開**: プラグインロード時、本体コアはプラグインの `defaultAssets()` を呼び出し、内蔵されているアセット（HTML, CSS, JS等）を `assets/overlay/[プラグイン名]/default/` 配下に自動的に書き出します。
- **上書き制限（安全第一）**: ユーザーのカスタマイズが消去されるのを防ぐため、**出力先に同名ファイルがすでに存在する場合、本体コアは自動での上書き（書き出し）を行いません。**
- **初期化**: ユーザーがデフォルト状態にリセットしたい場合は、ローカルの `default/` フォルダをフォルダごと削除してアプリを再ロード（または再起動）することで、再度デフォルトファイルが自動生成されます。

### 6.3. HTTP配信仕様
* **接続ポートおよび配信URL**:
  - デフォルトアセット: `http://[ホスト名]:58081/overlay/[プラグイン名]/default/index.html`
  - カスタムアセット: `http://[ホスト名]:58081/overlay/[プラグイン名]/custom/index.html`
* **LAN内マルチPC接続仕様**:
  - 同一LAN内の別PCに配置されたOBSからの接続を受け入れるため、本体HTTPサーバーは `QHostAddress::Any`（`0.0.0.0`）にバインドしてすべてのネットワークインターフェースからのリクエストをリッスンします。
  - クロスドメインエラーを防止するため、HTTP応答には `Access-Control-Allow-Origin: *` などのCORS許可ヘッダーを付与します。





