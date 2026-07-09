# TwitchChannelManagementTool 詳細設計書 (ドラフト)

本ドキュメントは、TwitchChannelManagementToolのアプリケーション本体コアにおける詳細設計（モジュール内部設計、スレッドモデル、クラス構成、通信シーケンス、データ構造）を定義するものです。

---

## 1. セキュリティ設計・ビルド構成 (TrustChain ＆ TransCipher)

本プロジェクトでは、コードの改ざん防止・出自証明のために **TrustChain** を、ローカル設定データの難読化のために **TransCipher** をそれぞれビルドパイプラインおよび実行プロセスに導入します。

* **CMake構成 (`CMakeLists.txt`)**:
  - `lib/TrustChain/cmake/trustchain.cmake` をインクルードし、ビルド時に自動で出自確認およびトークン検証を行います。
  - `trustchain_credentials.cmake`（Git管理外）から、システム名および認証トークン情報を安全に読み込みます。
  - `lib/TransCipher-Dist` をインクルードディレクトリおよびリンクライブラリに追加します。
  - **コンソール非表示設定 (Windows Subsystem)**:
    - アプリ起動時に背後に不要な黒いコンソール（コマンドプロンプト）画面が表示されるのを防ぐため、製品版実行ファイルおよびテストモード実行ファイルに対して、CMakeの `add_executable(... WIN32 ...)` を指定して Windows サブシステム（GUIアプリケーション）としてビルドします。
  - **テストモード・通常ビルドの分離**:
    - CMake オプション `option(ENABLE_TEST_MODE "Build with stubs instead of real network services" OFF)` を定義します。
    - `ENABLE_TEST_MODE` が `ON` の場合、実際のインターネット通信を行うクラス（`TwitchEventCollector`, `ObsHttpWebSocketServer`, `TtsManager`）をモック・スタブに差し替え、本物のサーバーを起動しなくても動作確認できるローカルデバッグ用の実行ファイル（`TwitchChannelManagementToolTestMode.exe`）をビルドします。
    - `BUILD_TESTING` オプションが `ON` の場合、自動テスト用のテストスイート（`run_unit_tests.exe`, `run_integration_tests.exe`）をビルドします。
    - 通常ビルド時（上記オプションが `OFF`）は、本物の外部連携を行う製品版実行ファイル（`TwitchChannelManagementTool.exe`）のみをビルドします。


* **権利署名適用 (実行時)**:
  - `main.cpp` でウィンドウ表示前に `TrustChain` の検証を通します。オンライン検証が成功し、バイナリ改ざんが検知されなければ正規ビルドとして動作します。
  - 改ざん検知または検証エラー時は、アプリのタイトルバーに `(Custom Build)` を付与し、非公式ビルドであることを明示します。

---

## 2. 画面設計・UI詳細設計 (MainWindow UI)

本体 UI は縦型サイドバーレイアウトを採用し、設定および各プラグインの画面を動的に配置します。

* **メインウィンドウの構造 (`MainWindow.ui`)**:
  - **ベース**: `QMainWindow`
  - **レイアウト**: `QHBoxLayout` を用いて、左側に「サイドメニューエリア」、右側に「メイン表示エリア」を配置。
  - **サイドメニューエリア**: `QListWidget` を用い、アイコンとテキストを縦並びに配置（スタイルシートでボタン風に装飾）。
  - **メイン表示エリア**: `QStackedWidget` を配置し、サイドメニューの選択に応じて表示するウィジェットを切り替える。
  - **共通ステータスバー (最下部)**: Twitchの接続状態、OBSサーバーの待機ポート、TTS接続状態を表示。

```
+--------------------------------------------------------------------------------+
| TwitchChannelManagementTool                                                    |
+------------+-------------------------------------------------------------------+
|  [設定]    | [共通設定（全体設定）]                                            |
|  [プラグA] |  ■ Twitch 接続設定                                                |
|  [プラグB] |    チャンネル名: [ twitch_streamer ]                              |
|            |    OAuth トークン: [ ******************** ]                       |
|            |  ■ OBS 連携ポート: [ 8081 ]                                       |
|            |  ■ 音声合成連携: [v] 棒読みちゃん  [ ] VOICEVOX                    |
|            |  ---------------------------------------------------------------  |
|            |  ■ プラグイン有効化設定                                           |
|            |    [v] 時報プラグイン (TimeSignalPlugin.dll)                      |
|            |    [v] コメント表示プラグイン (CommentsTtsPlugin.dll)             |
|            |    [ ] モデレーションプラグイン (ModerationPlugin.dll)            |
+------------+-------------------------------------------------------------------+
| 接続ステータス: Twitch 接続中 | OBS ポート: 8081 | TTS: 接続完了               |
+--------------------------------------------------------------------------------+
```

---

## 3. クラス設計・詳細インターフェース

### 3.1. スレッドモデルとサービス層
重い通信処理による UI の応答性低下を防ぐため、サービスモジュールは `QThread` 上で稼働させ、コアとは Qt のシグナル・スロット（スレッド間通信時は自動的に `QueuedConnection`）でやり取りします。

#### `TwitchEventCollector` (スレッド稼働)
Twitch API（Helix）へのリクエストおよび EventSub WebSocket との常時接続を維持するモジュール。
* **主要シグナル**:
  * `void commentReceived(const TwitchComment& comment);`
  * `void rewardRedeemed(const TwitchRewardRedemption& redemption);`
* **主要メソッド（スロット経由で呼び出し）**:
  * `void connectToTwitch(const QString& channelName, const QString& oauthToken);`
  * `void disconnectFromTwitch();`
  * `void postChatMessage(const QString& message);`
* **OAuth認証用リダイレクトおよび臨時リスナー制御**:
  - `http://localhost:58080/` (ポート番号は `src/core/twitch_credentials.h` の `TWITCH_REDIRECT_PORT` 定数を使用)
  - **臨時HTTPサーバー制御**: 接続開始時、本体コアは臨時で `QTcpServer` を起動し、ポート `58080` をリスンします。
  - **URLフラグメント（ハッシュ）処理フロー**: TwitchはImplicit Grantにより `#access_token=...` を返しますが、これはHTTPリクエストでサーバーに届きません。そのため、臨時サーバーは最初の `GET /` に対し、以下のJavaScriptリダイレクトを含むシンプルなレスポンスを応答します。
    `<html><script>window.location.href = "/token?" + window.location.hash.substring(1);</script></html>`
  - **トークン取得とクリーンアップ**: 再リクエストされた `GET /token?access_token=...` からクエリパラメータを取り出し、`oauthToken` を抽出・保存した上で「認証成功」のHTMLを応答し、臨時サーバーを即座にクローズ（停止）します。



#### `ObsHttpWebSocketServer` (スレッド稼働)
OBSのブラウザソース向けにHTTPサーバーおよびWebSocketサーバーを提供するモジュール。
* **主要メソッド**:
  * `void startServer(quint16 port);`
  * `void stopServer();`
  * `void broadcastToObs(const QString& actionType, const QJsonObject& payload);`
* **マルチPC連携および複数クライアント接続制御**:
  - **全インターフェースへのバインド**: 同一LAN内の別PCからの接続を受け入れるため、ソケットのバインド先は `QHostAddress::Any` (0.0.0.0) を使用します。
  - **CORSヘッダーの付与**: HTTP応答のヘッダーに `Access-Control-Allow-Origin: *` を設定し、別PC上のOBSで発生するクロスオリジン制限（CORS）をクリアします。
  - **複数クライアント管理**: 接続されたクライアントを管理するために `QList<QWebSocket*>` を保持し、接続確立時（`newConnection`）にリスト追加、切断時（`disconnected`）に自動削除します。`broadcastToObs` 実行時は、接続中のすべての有効なクライアントへ非同期ブロードキャスト送信を行います。


#### `TtsManager` ＆ 音声合成クライアント群 (スレッド稼働)
音声合成（TTS）連携処理は、共通の抽象インターフェース `ITtsClient` と、ツールごとの固有実装クラス（`BouyomiClient`, `VoiceVoxClient`）に分割し、`TtsManager` がこれらを動的に切り替えて制御します。

##### 1. `ITtsClient` (抽象クラス・共通インターフェース)
各音声合成ツールの共通APIを定義する基底クラス。
* `virtual ~ITtsClient() = default;`
* `virtual bool initialize(const QJsonObject& settings) = 0;` (各固有設定を用いた接続初期化)
* `virtual void speak(const QString& text, const QString& speakerId, int speed, int pitch, int volume) = 0;` (発話要求)
* `virtual void stop() = 0;` (発話強制停止・切断)

##### 2. `BouyomiClient` (棒読みちゃん用固有実装クラス)
* `ITtsClient` を継承。
* 棒読みちゃんの専用バイナリプロトコル（TCPソケット通信）を用いて、指定のポート・パラメータ（速度、音程、音量、声種）でソケットパケットを送信します。

##### 3. `VoiceVoxClient` (VOICEVOX用固有実装クラス)
* `ITtsClient` を継承。
* `QNetworkAccessManager` を用い、VOICEVOXのHTTP REST API（ローカルサーバー）に対して以下のリクエストを送信します。
  1. `POST /audio_query?text={text}&speaker={speakerId}`: クエリ生成。JSONペイロードを取得。
  2. JSON内の `speedScale` (速度), `pitchScale` (音高), `volumeScale` (音量) をユーザー設定パラメータに書き換える。
  3. `POST /synthesis?speaker={speakerId}`: 音声合成実行。WAVバイナリデータを取得。
  4. 取得した音声を再生デバイス等へ出力（またはVOICEVOX標準再生機能経由での再生制御）。

##### 4. `TtsManager` (スレッド制御・窓口クラス)
他のモジュールからの発話イベントを受け取る窓口クラス。バックグラウンドスレッドで稼働。
* **主要メソッド（スロット経由）**:
  * `void requestSpeak(const QString& text, const QString& speakerId, int speed, int pitch, int volume);`
  * `void updateSettings(int activeEngineType, const QJsonObject& settings);` (エンジンの動的切り替えと、アクティブなクライアントの再初期化処理)


---

### 3.2. 通信データ構造 (下りデータモデル)

```cpp
// Twitch のチャットコメント構造体
struct TwitchComment {
    QString id;           // コメントID
    QString userId;       // ユーザーID
    QString username;     // ユーザーログインID (英数字)
    QString displayName;  // ユーザー表示名 (日本語等)
    QString comment;      // コメント本文
    QString avatarUrl;    // アバターアイコンのURL
    QJsonArray badges;    // バッジ情報の配列 (url, label を含むオブジェクト)
    qint64 timestamp;     // 受信エポックミリ秒
};

// チャンネルポイント引き換え構造体
struct TwitchRewardRedemption {
    QString id;           // 引き換えID
    QString rewardId;     // カスタム報酬ID
    QString rewardName;   // カスタム報酬名
    QString userId;       // リスナーユーザーID
    QString username;     // リスナーログインID
    QString displayName;  // リスナー表示名
    QString userInput;    // リスナー入力テキスト (ある場合)
    qint64 timestamp;     // 受信エポックミリ秒
};
```

---

### 3.3. 上り通信用カスタムイベント (`PluginNotifyEvent`)

プラグイン（別DLL）から本体コアへ処理を非同期要求するために、独自の `QEvent` を定義します。

```cpp
#include <QEvent>
#include <QJsonObject>

class PluginNotifyEvent : public QEvent {
public:
    enum RequestType {
        TypeTtsSpeak = QEvent::User + 100, // TTS読み上げ要求
        TypeObsBroadcast,                  // OBSオーバーレイへの描画・演出要求
        TypeSendChat,                      // Twitchへのチャット送信要求
        TypeDiscordWebhook                 // Discord Webhook送信要求
    };

    PluginNotifyEvent(RequestType type, const QJsonObject& payload)
        : QEvent(static_cast<QEvent::Type>(type)), m_payload(payload) {}

    QJsonObject payload() const { return m_payload; }

private:
    QJsonObject m_payload;
};
```

* **イベントペイロードの設計**:
  * **`TypeTtsSpeak`**:
    `{ "text": "読み上げ内容", "speakerId": "1", "speed": 100, "pitch": 100, "volume": 50 }`
  * **`TypeObsBroadcast`**:
    `{ "action": "show_alert", "data": { "image": "path/to/img", "duration": 5 } }` (データ構造は汎用形式に準拠)
  * **`TypeSendChat`**:
    `{ "message": "送信するチャット内容" }`
  * **`TypeDiscordWebhook`**:
    `{ "url": "https://discord.com/api/webhooks/...", "payload": { "content": "送信メッセージ内容 (または任意のDiscord Webhook用JSON構造体)" } }`

### 3.4. Discord Webhook送信代行の処理仕様
本体コアの `AppController` またはネットワーク管理モジュールは、`TypeDiscordWebhook` イベントを検知した際、以下の非同期 HTTP POST 送信処理を実行します。

1. **イベントの受信**:
   - プラグインが `ICoreContext::postDiscordWebhook(url, payload)` を呼び出すと、本体コア内部で `TypeDiscordWebhook` をリクエスト種別とする `PluginNotifyEvent` がインスタンス化され、`QCoreApplication::postEvent(appController, event)` でコアに非同期送信されます。
2. **非同期送信処理**:
   - コアのイベントハンドラ `customEvent(QEvent* event)` 内で `TypeDiscordWebhook` をキャッチします。
   - `QNetworkAccessManager` を使用し、イベントから抽出した `url` に対して HTTP POST リクエストを発行します。
     ```cpp
     QNetworkRequest request(QUrl(url));
     request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
     
     QJsonDocument doc(payload);
     QByteArray body = doc.toJson(QJsonDocument::Compact);
     
     QNetworkReply* reply = m_networkAccessManager->post(request, body);
     ```
3. **応答の処理**:
   - 送信は非同期で行われるため、UIスレッドをブロックしません。
   - `QNetworkReply` の `finished` シグナルを接続し、応答ステータスを確認します。送信エラー（ネットワーク切断等）が発生した場合は、`Logger` モジュールを介してエラーログをファイル出力します。

---

## 4. プラグインローダー ＆ 設定管理詳細

### 4.1. 動的ロード・アンロード・順序制御プロセス (再起動不要)

本アプリは、設定画面での有効化チェック・順序変更の適用時に、アプリケーションを再起動することなくその場で動的にプラグインのロード/アンロード、および表示順の並び替えを実行します。

#### A. 動的ロード（有効化）処理手順
1. ユーザーが設定画面でプラグインを「有効化」し `[設定保存]` をクリックした際、または起動時にロード設定になっている場合、対象DLLを `QPluginLoader` でメモリ上にロードします。
2. インスタンスが `IChannelPlugin` に正常キャストできるか検証します。
3. ロード成功後、`initialize(ICoreContext* context)` を呼び出してコアの参照をプラグインに渡します。
4. `SignalDispatcher` の下りシグナル（コメント受信、チャンネルポイント引換、タイマー）をプラグインの各ハンドラメソッドへQtシグナル・スロット接続します。
5. プラグインから `iconPngData()` を呼び出して生のPNG画像データ（`QByteArray`）を取得し、`QPixmap::loadFromData` で `QPixmap` / `QIcon` に展開します。
6. `MainWindow` の左サイドバー（`QListWidget`）の「設定」タブの**直前（上隣）**に、プラグイン名と生成したアイコンで新しい選択項目を動的にインサートします。
7. プラグインの `createWidget(this)` を呼んでカスタム `QWidget` 画面を取得し、`QStackedWidget` に追加し、サイドバー項目とインデックスを紐付けます。

#### B. 動的アンロード（無効化）処理手順
1. ユーザーが設定画面でプラグインを「無効化」し `[設定保存]` をクリックした際、まず当該プラグインの下りシグナル接続をすべて切断（`disconnect`）します。
2. `MainWindow` の左サイドバーから該当タブ項目を削除し、`QStackedWidget` から画面を取り除きます。
3. **【重要：メモリクラッシュ防止】** `QPluginLoader::unload()` を呼ぶ前に、**プラグインが生成した `QWidget`（画面インスタンス）を確実に `delete`（メモリ上から消去）** します。ウィジェットが存在したままDLLを解放すると無効メモリへのアクセスでクラッシュするため、この順序を厳守します。
4. `shutdown()` を呼び出してプラグイン内部のクリーンアップを実行します。
5. `QPluginLoader::unload()` を実行し、DLLを安全にアンロードします。

#### C. 動的順序変更（並び替え）処理手順
1. `SettingsTab` 内の `[上へ]` `[下へ]` ボタンで順序を変更し適用した際、DLLのロード・アンロードは行いません。
2. 左サイドバー（`QListWidget`）内の表示アイテム順、および `QStackedWidget` 内のインデックス位置の対応関係を、設定リストに定義されたファイル名順に沿って再構成（ソート）します。これにより、メモリ状態を維持したまま、見た目上の表示優先順を即座に変更します。
3. 設定データ（ロード順序、有効化状態）は本体設定ファイル（`settings.bin`）に保存されます。

### 4.2. TransCipher による暗号化設定の保存・復元 (ホスト委譲型 API)
* **難読化キーの保護と暗号化処理の一元管理**:
  プラグイン自体が暗号化キーをハンドリングしたり、TransCipher SDKのAPIを直接呼び出したりすることを防止し、ホストプログラム（本体コア）内部で暗号処理を一元管理します。
* **プラグインの暗号化ファイル書き込み手順**:
  1. プラグインは、保存したい任意のデータ（平文 JSON テキストなど）を `QByteArray` に展開する。
  2. プラグインは `ICoreContext::writeEncryptedFile("ファイル名", 平文データ)` を呼び出す。
  3. ホスト（本体コア）側でパスの安全検証を行い、プラグインディレクトリ配下を対象に TransCipher (`CipherEngine::encrypt`) で暗号化を施し、ファイルを上書き保存（新規作成・上書き）する。
* **プラグインの復号読み込み手順**:
  1. プラグインは `ICoreContext::readEncryptedFile("ファイル名")` を呼び出す。
  2. ホスト（本体コア）側で指定ファイルを読み込み、TransCipher (`CipherEngine::decrypt`) で復号を行い、平文データを返却する。（ファイルが存在しない、または復号エラー時は空の `QByteArray` を安全に返却する）。
* **安全対策 (DPAPIの併用)**:
  本体の「Twitch OAuthトークン」など極めて強固な保護が必要なグローバル設定値は、Windows API の `CryptProtectData` (DPAPI) を使用して難読化の上で `settings.bin` に保存します。非Windows環境下でのみ、`TransCipher` による難読化方式に自動的に切り替えます。

### 4.3. カレントディレクトリの基準化（実行ファイル基準の絶対パス化）
本アプリ内で指定されるすべての設定ファイル（`debug_config.json`, `settings.bin`）、ログ出力ディレクトリ（`logs/`）、アセット展開ディレクトリ（`assets/`）、プラグインDLLフォルダ（`plugins/`）の物理パス指定は、起動時カレントディレクトリに依存する相対指定（`QDir::current()`）を完全に排除します。
パスを取得する際は、必ず **`QCoreApplication::applicationDirPath()`** （実行ファイル `TwitchChannelManagementTool.exe` が置かれているフォルダの絶対パス）をベースとして絶対パスを組み立てるロジックに統一します。これにより、別フォルダからの実行やショートカット起動時のパスズレ問題を根絶します。

---

## 5. ログ出力詳細設計 (本体コア・Loggerモジュール)


### 5.1. `Logger` クラス (`src/modules/logger/Logger.h` / `.cpp`)
ログ関連処理は、他のモジュールやコア本体から独立した「`Logger` モジュール」として `src/modules/logger/` ディレクトリ内に完全にモジュール分割し、低結合な設計とします。
本体および各連携サービスは、本モジュールのシングルトンインスタンスを通じて難読化ログを書き出します。

```cpp
class Logger {
public:
    static Logger& instance();
    
    // ログ記録メインメソッド
    void log(const QString& action, const QString& className, const QString& funcName, const QString& description);

private:
    Logger();
    ~Logger() = default;
    
    bool m_enableLogging;   // ログ出力の有効フラグ
    QByteArray m_cipherKey; // 初期化時に読み込まれる難読化キー
    void loadDebugConfig(); // 平文設定ファイルの読み込み
};
```

* **平文設定ファイルによるログ有効判定 (`debug_config.json`)**:
  - アプリケーション起動時、`Logger` のコンストラクタ内で実行ファイルと同階層にある平文の JSON 設定ファイル `debug_config.json` をロードします（本ファイルは TransCipher による難読化を行いません）。
  - `debug_config.json` のキー `enable_logging` が `true` の場合のみ、ログ出力を有効（`m_enableLogging = true`）にします。存在しない場合や `false` の場合は、ログ出力を一切行わず早期リターンします。
  - `debug_config.json` のフォーマット例：
    ```json
    {
        "enable_logging": true
    }
    ```

* **ログの難読化・ファイル保存処理**:
  1. `m_enableLogging` が `false` の場合は、何も行わずに即座にリターンする。
  2. `QDateTime::currentDateTime()` から、`YYYY-MM` および `DD-HH-mm-ss` 形式の文字列を取得する。
  3. 以下の平文ログエントリを生成する。
     `QString plainLine = QString("[%1][%2][%3][%4][%5][%6]")
                          .arg(dateMonth)     // YYYY-MM
                          .arg(timeDetail)    // DD-HH-mm-ss
                          .arg(action)        // INFO / ERROR 等
                          .arg(className)     // クラス名
                          .arg(funcName)      // 関数名
                          .arg(description);  // 詳細文`
  4. `TransCipher` の `CipherEngine::encrypt` を使用して、`plainLine.toUtf8()` を難読化する。
     `CipherResult result = CipherEngine::encrypt(plainLine.toUtf8(), m_cipherKey, AesMode::Mandatory);`
  5. 暗号化バイナリ `result.data()` を Base64 文字列にエンコードし、末尾に改行文字 `\n` を付加する。
  6. `logs/` ディレクトリ配下の `{dateMonth}.log` ファイルに、追記モードで書き込みを行う。
     ※ ログ出力の1回ごとに1行の Base64 文字列として追記するため、読み込み側では改行区切りで1行ずつ取り出して復号することが可能です。

---

## 6. プラグイン・アセット配信詳細設計 (OBS連携用)

### 6.1. `defaultAssets()` インターフェース実装詳細
プラグイン側は自らのリソース（`.qrc` 経由等）から、初期アセット（HTML/CSS/JS等）をバイナリとして返却します。

```cpp
QMap<QString, QByteArray> defaultAssets() const override {
    QMap<QString, QByteArray> assets;
    
    // Qtリソースから読み出すヘルパー処理
    QFile fileHtml(":/overlay/index.html");
    if (fileHtml.open(QIODevice::ReadOnly)) {
        assets["overlay/index.html"] = fileHtml.readAll();
    }
    QFile fileCss(":/overlay/style.css");
    if (fileCss.open(QIODevice::ReadOnly)) {
        assets["overlay/style.css"] = fileCss.readAll();
    }
    return assets;
}
```

### 6.2. 本体コア側のアセット抽出処理シーケンス
`PluginLoader` がプラグインDLLの初期化に成功した後、以下のシーケンスを実行します。

1. プラグインの `defaultAssets()` からアセットデータを取得。
2. 返されたマップの各項目（`relativePath`、`data`）について、以下のローカルパスを決定する。
   `QString outPath = QDir(QCoreApplication::applicationDirPath()).filePath("assets/overlay/" + pluginInstance->pluginName() + "/default/" + relativePath);`
3. 対象ファイル `outPath` が**既に存在するか確認** (`QFile::exists(outPath)`)。
4. ファイルが存在しない場合：
   - ディレクトリを作成: `QDir().mkpath(QFileInfo(outPath).absolutePath());`
   - `QFile` を開いてバイナリデータを書き出し保存。
5. ファイルが既に存在する場合は、上書きを行わずにスキップする（ユーザーカスタマイズの保護）。

### 6.3. `ObsHttpWebSocketServer` による静的ファイル配信
OBSのブラウザソース等が本サーバーにアクセスした際、リクエストパスに応じてローカルファイルを返却します。

* **URLルーティング仕様**:
  - リクエストパス: `/overlay/[プラグイン名]/[default または custom]/[ファイル名]`
  - マッピング先物理ファイルパス:
    `QDir(QCoreApplication::applicationDirPath()).filePath("assets/overlay/[プラグイン名]/[default または custom]/[ファイル名]")`
  
* **ファイル配信処理**:
  1. マッピングされた物理パスにファイルが存在するかチェックする。
  2. 存在する場合、ファイルを読み込み、応答ヘッダーに `Access-Control-Allow-Origin: *` を設定し、さらに拡張子に対応した `Content-Type`（`text/html`, `text/css`, `application/javascript`, `image/png` 等）を設定して HTTP 200 OK で応答する。
  3. ファイルが存在しない場合、HTTP 404 Not Found を応答する。

---

## 7. ウィンドウ状態の保存・復元詳細仕様

`MainWindow` の終了（閉じる）時および起動初期化時に、以下のシーケンスでウィンドウ状態の永続化を実行します。

### 7.1. 終了時の状態保存シーケンス
1. ユーザーが閉じるボタン等をクリックして `closeEvent(QCloseEvent* event)` がトリガーされる。
2. `MainWindow` は `saveGeometry()` を呼んでウィンドウの位置とサイズを取得する。
3. `MainWindow` は `saveState()` を呼んで最大化、最小化、および各ドックウィンドウの配置状態を取得する。
4. 取得された `QByteArray` データをJSON（Base64エンコード）等に変換し、設定管理モジュールを通じて `settings.bin` へ安全に暗号化保存（Windows環境下ではDPAPIを使用）する。
5. 各スレッド・プラグインのリソースのクリーンアップを実行し、イベントを `event->accept()` して終了処理を完了する。

### 7.2. 起動初期化時の状態復元シーケンス
1. `MainWindow` のコンストラクタ内で、UIウィジェットの初期化（`setupUi(this)`）が完了する。
2. 設定ファイル `settings.bin` を復号ロードする。
3. 保存されている `geometry` および `state` バイナリデータを読み取る。
4. データが存在する場合、`restoreGeometry(savedGeometry)` および `restoreState(savedState)` を呼び出して以前の位置・サイズ・状態を正確に復元する。
5. 位置が画面外に外れてしまっている場合のフェールセーフとして、復元後に `QGuiApplication::primaryScreen()->geometry()` と交差しているかを確認し、完全に画面外であればデフォルトの中央位置に強制リセットする。

