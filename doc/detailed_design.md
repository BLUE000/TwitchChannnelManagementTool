# Twitchコメント解析ツール 詳細設計書

ウォーターフォール Vモデルにおける「詳細設計（モジュール内部設計・DBスキーマ・クラス設計・シーケンス）」を定義します。

---

## 1. セキュリティ設計・ビルド構成 (TrustChain導入)

本アプリケーションの「出自証明」および「改ざん防止・再配布対策」として、**TrustChain** を導入します。

* **CMake構成**: 
  - プロジェクトのサブディレクトリとして `TrustChain` を追加（`add_subdirectory`）し、ビルド時に自動で出自確認およびトークン埋め込みを実施する。
  - メインアプリのほか、**付属ツール（DB Viewer）**も別EXEとして同一プロジェクト（CMake）内でビルド可能にする。
  - `trustchain_credentials.cmake` を利用し、GitHubリポジトリおよびTransCipherサーバーのURL、秘密鍵を安全に管理（Git管理外とする）。
* **第3層プロテクト（バイナリ透かし）**:
  - アプリ（`.exe`）完成後、ビルドプロセスに同梱される `BinMarkManager` を利用して物理的な権利情報（ウォーターマーク）をバイナリに埋め込むフローを採用する。

---

## 2. 画面設計 (UI Layout)

`MainWindow.ui` の構成要素：

* **基盤**: `QMainWindow`
* **セキュリティ対応**: 
  - アプリ起動時に `TrustChain::QtHelper::applyWatermark(&w, status)` を適用。改ざんやオフライン状態が検知された場合は、タイトルバーに `(Custom Build)` を付与し、ステータスバーに権利表記を強制表示する。
* **メイン領域**: `QTabWidget`（以下の4タブ構成）

### 2.1 ビューワタブ（旧コメントタブ）
* `tableViewComments` (QTableView): コメントをリアルタイム表示。右クリックでコンテキストメニューを起動し、ピン留め操作が可能。
* **チャットアナウンスエリア (最下部)**:
  - 横並びレイアウト (`QHBoxLayout`) を配置。
  - `comboAnnouncementColor` (QComboBox): アナウンス背景色を選択。
    - 選択肢: 通常 (`primary`), 青 (`blue`), 緑 (`green`), オレンジ (`orange`), 紫 (`purple`)
  - `lineAnnouncementText` (QLineEdit): メッセージ入力欄（最大500文字の文字数制限プレースホルダーあり）。
  - `btnSendAnnouncement` (QPushButton): アナウンス送信実行ボタン。

### 2.2 視聴者タブ
* 手動「更新」ボタン、および最終更新時間ラベル。
* `QScrollArea` と 7つのアコーディオン（すべて/ストリーマー/モデレーター/VIP/アーティスト/チャットボット/一般）。
* 各アコーディオンは `QPushButton`（トグル）と `QListWidget` で構成。
* ストリーマーを除く視聴者行にカスタム行ウィジェット（`ChatterRowWidget`）を配置し、マウスホバー時または行選択時に右端にアクションボタンを表示。
  - `[シャウトアウト] [VIP] [モデレータ]  (余白)  [タイムアウト] [BAN]` の配置と余白制御。
  - VIP・モデレータ・BANはトグル動作とし、状態を表現。
  - タイムアウト時は時間指定ダイアログを起動。
  - モデレータ追加/削除、タイムアウト決定後、BAN時には実行確認メッセージ（`QMessageBox`）を表示。
  - シャウトアウト時は、2分間のクールタイム表示（ウィンドウタイトルバーでのリアルタイムカウントダウン）を制御。
  - 視聴者が複数の属性を持つ場合（例: Nightbotはモデレーターかつチャットボット）、それぞれの `QListWidget` に重複して登録・表示する。

### 2.3 解析タブ
* 左側縦タブ（`QTabWidget`、ポジション: West）でサブタブを切り替え可能。
  - **解析ログサブタブ**: `QTextBrowser`（トレンド・スパム等の検知ログ表示）
  - **コメント集計サブタブ**:
    - 表示グラフ切り替え (`comboGraphType`): 総コメント数推移 / ユーザーごとの推移
    - 時間粒度切り替え (`comboGranularity`): 自動 / 5秒 / 10秒 / 30秒 / 1分 / 5分
    - 表示数切り替え (`comboTopN`): TOP 3 / TOP 5 / TOP 10 / 全員表示
    - コメント総数表示ラベル (`labelTotalComments`)
    - ユーザー別コメント数ランキング (`tableUserStats`)
    - 折れ線グラフ表示エリア (`chartView`): `QtCharts` を用いた描画。ウィンドウリサイズ時に `resizeEvent` をトリガーとし、X軸・Y軸の目盛り間隔を動的に再計算して描画領域に適合させる（ウィンドウサイズ自体の自動調整は行わない）。

### 2.4 設定タブ
* Twitch接続設定（OAuth連携開始ボタン、チャンネル名入力など）。
* **読み上げ（TTS）設定**:
  - `groupBoxTts` (アコーディオン/トグルボタン `btnToggleTts` で開閉) 内に配置。
  - **連携ツール選択**: ラジオボタン `radUseBouyomi`（棒読みちゃんを使用する）および `radUseVoicevox`（VOICEVOXを使用する）の選択式。
  - **エンジン個別設定領域**:
    - 棒読みちゃん用: ホスト名、ポート、実行ファイルパス（および参照ボタン）、音声種類選択（QComboBox）。
    - VOICEVOX用: ホスト名、ポート、実行ファイルパス（および参照ボタン）、話者ID（および話者選択リスト/QComboBox）。
  - **共通UIコントロール**:
    - 音量 (QSpinBox): 棒読みちゃん時は -1〜100、VOICEVOX時は 0〜100。
    - 速度/スピード (QDoubleSpinBox): 棒読みちゃん時は 50〜300 %、VOICEVOX時は 0.5〜2.0 倍。
    - 音程/ピッチ (QDoubleSpinBox): 棒読みちゃん時は 50〜200 %、VOICEVOX時は -0.15〜0.15。
    - 設定値は各エンジンごとに個別に保持され、ラジオボタン切り替え時に動的にUI表示範囲（最小値・最大値・ステップ・単位ラベル）を調整した上でロードされる。
    - 自動起動/自動終了チェックボックス: `chkTtsAutoStart`（「起動時に読み上げツールを自動起動する」）、`chkTtsAutoStop`（「終了時に読み上げツールを自動終了する」）。
  - **共通アクションエリア**:
    - 除外ユーザー設定 `editTtsIgnoreUsers` (QLineEdit): 読み上げを無視するユーザー名のカンマ区切り入力欄。
    - テスト送信エリア: テストテキスト入力用 QLineEdit と 「テスト送信」ボタン（`btnTestTts`）。
    - 保存ボタン `btnSaveTts`: アクティブなラジオボタンの状態に基づき、個別および共通設定を対象のTTSエンジン設定として `config.json` に保存。
* **BOT除外設定**:
  - `editBotUsers` (QLineEdit): BOT判定するユーザー名のカンマ区切り入力欄。
* **OBS連携設定・物理アバター設定**:
  - `groupBoxObs` (QGroupBox / トグルボタン `btnToggleObs` で開閉) 内に配置。
  - `chkObsFileOutput` (QCheckBox): コメントをテキストファイルに出力する。
  - `spinObsPort` (QSpinBox): HTTPサーバーポート（デフォルト：8081）。
  - `comboObsOverlay` (QComboBox): 使用するオーバーレイHTMLファイル名を選択。
  - `btnOpenOverlayFolder` (QPushButton): `assets/overlay/` フォルダをエクスプローラで開く。
  - `chkObsWebSocket` (QCheckBox): OBSブラウザソース連携（WebSocketサーバー）を有効にする。
  - `editObsUrl` (QLineEdit): ブラウザソースURL（読み取り専用、例：`http://localhost:8081/`）。
  - `btnCopyObsUrl` (QPushButton): 上記のURLをクリップボードにコピー。
  - `btnSaveObs` (QPushButton): 上記の設定を `config.json` に保存する。
  - **物理アバターオーバーレイ設定領域**:
    - `groupBoxObsPhysics` (QGroupBox): 物理アバターオーバーレイの調整パネル（`comboObsOverlay` で `physics/index.html` が選択されている時のみ表示制御）。
    - `spinObsAvatarMinSize` (QSpinBox): アバターの最小サイズを設定（デフォルト 50 px）。
    - `spinObsAvatarMaxSize` (QSpinBox): アバターの最大サイズを設定（デフォルト 150 px）。
    - `spinObsBounceFactor` (QSpinBox): 地面衝突時の跳ね返りやすさを設定（0〜100%、デフォルト 30%）。
    - `lblObsPhysicsPreview` (QLabel): 最小・最大サイズのピクセル設定に応じ、`サイズプレビュー: [ 50px 〜 150px ]` のように動的に表示を更新するラベル。
* **権利・ライセンス表記（TrustChain・Qt等のライセンスを記載）。**

---

## 3. データ保護と資格情報管理設計

### 3.1 難読化
* プライバシー保護およびセキュリティの観点から、DBのみならずシステムが生成する中間ファイル（ログの一時キャッシュ、設定ファイル、集計用の一時エクスポート等）について、`TransCipher-Dist` を利用して難読化して保存します。

### 3.2 セキュアOAuthトークン管理 (Windows DPAPI)
* ユーザーのOAuthアクセストークンは、Windows環境においてはWindowsデータ保護API（DPAPI: `CryptProtectData` および `CryptUnprotectData`）を使用してローカルユーザーアカウントに紐づく鍵で暗号化します。
* 暗号化されたトークンデータは、実行バイナリと同ディレクトリ内の `tokens.enc` ファイルにバイナリ保存されます。
* Windows以外のOSでは、`TransCipher-Dist` による難読化方式にフォールバックします。

### 3.3 データベーススキーマ設計 (SQLite)
個人特定が可能なカラムも同様に `TransCipher-Dist` により難読化して保存します。

#### テーブル: `chat_logs`
* `id` (INTEGER PRIMARY KEY AUTOINCREMENT)
* `timestamp` (DATETIME) - 受信日時
* `user_id` (TEXT) - TwitchのユーザーID（一意）
* `username_enc` (BLOB) - 発言者名（暗号化済）
* `message_enc` (BLOB) - 発言内容（暗号化済）
* `sentiment_score` (REAL) - 感情スコア（-1.0 ~ 1.0）
* `is_spam` (INTEGER) - スパム判定フラグ（0=正常, 1=スパム）

#### テーブル: `user_stats`
* `user_id` (TEXT PRIMARY KEY) - TwitchのユーザーID
* `username_enc` (BLOB) - 発言者名（暗号化済）
* `total_comments` (INTEGER) - 累計コメント数
* `last_seen` (DATETIME) - 最終発言日時

---

## 4. クラス設計・インターフェース設計

### 4.1 アプリケーション初期化層 (main.cpp)
* `main()` 関数内において、UIの生成より前に `TrustChain::Core guard;` をインスタンス化し、`guard.verifyToken()` によりオンラインでの証明書バリデーションおよびヌル文字汚染スキャンを実施する。

### 4.2 外部通信インターフェース群
#### `ITwitchEventCollector` (Interface)
* `virtual void connectToTwitch() = 0;`
* `virtual void disconnectFromTwitch() = 0;`
* `virtual void requestChatters() = 0;`
* `virtual void sendShoutout(const QString& toBroadcasterId) = 0;`
* `virtual void setVipStatus(const QString& targetUserId, bool enable) = 0;`
* `virtual void setModeratorStatus(const QString& targetUserId, bool enable) = 0;`
* `virtual void banUser(const QString& targetUserId, int duration, const QString& reason = "") = 0;`
* `virtual void unbanUser(const QString& targetUserId) = 0;`
* `virtual void pinChatMessage(const QString& messageId, int durationSeconds) = 0;`
* `virtual void unpinChatMessage(const QString& messageId) = 0;`
* `virtual void sendAnnouncement(const QString& message, const QString& color) = 0;` // 新規追加

#### `TwitchEventCollectorImpl` (ITwitchEventCollectorの実装クラス)
* QWebSocket と QNetworkAccessManager を保持し、EventSubの購読およびHelix REST APIを呼び出す。
* **新規API追加点**:
  - `sendAnnouncement(const QString& message, const QString& color)`:
    - エンドポイント: `POST https://api.twitch.tv/helix/chat/announcements?broadcaster_id={broadcaster_id}&moderator_id={moderator_id}`
    - ペイロード JSON: `{ "message": "...", "color": "..." }`
    - 送信時ヘッダー: Client-Id, Authorization (Bearer), Content-Type (application/json)

### 4.3 内部処理モジュールクラス
#### `ConfigManager`
* `loadConfig()` / `saveConfig()`: 設定をJSON形式で難読化保存・復元。
  - `activeTtsEngine`: `0` (なし), `1` (棒読みちゃん), `2` (VOICEVOX)
  - 棒読みちゃん用個別設定: Host, Port, Exe, Voice, Volume, Speed, Pitch
  - VOICEVOX用個別設定: Host, Port, Exe, Speaker, Volume, Speed, Pitch
  - 共通設定: `ttsAutoStart`（自動起動）、`ttsAutoStop`（自動終了）、`ttsIgnoreUsers`（除外ユーザー）
  - OBS物理オーバーレイ設定:
    - `obs_avatar_min_size`（最小サイズ, デフォルト 50）
    - `obs_avatar_max_size`（最大サイズ, デフォルト 150）
    - `obs_bounce_factor`（反発係数 %, デフォルト 30）
    - `obs_browser_width`（OBSブラウザ解像度幅, デフォルト 800）
    - `obs_browser_height`（OBSブラウザ解像度高さ, デフォルト 600）
    - `obs_effect_symbols`（ダブルクリック時表示記号, デフォルト `"♥,♦,♣,♠"`)
    - `obs_effect_size`（パーティクルサイズ, デフォルト 20）
    - `obs_effect_count`（放出パーティクル数, デフォルト 5）
    - `obs_effect_opacity`（パーティクルの不透明度/濃さ, デフォルト 100）
* `saveToken(const QString& token)` / `loadToken()`: DPAPI / TransCipherを用いた暗号化トークンの保存とロード。
* `getBotUsers()` / `setBotUsers(const QStringList& bots)`: BOT除外判定用リストの取得・更新。
* `getTtsIgnoreUsers()` / `setTtsIgnoreUsers(const QStringList& users)`: 読み上げ無視ユーザーリストの取得・更新。
* 物理・エフェクト設定 Getter/Setter:
  - `getObsAvatarMinSize() const` / `setObsAvatarMinSize(int size)`
  - `getObsAvatarMaxSize() const` / `setObsAvatarMaxSize(int size)`
  - `getObsBounceFactor() const` / `setObsBounceFactor(int factor)`
  - `getObsBrowserWidth() const` / `setObsBrowserWidth(int width)`
  - `getObsBrowserHeight() const` / `setObsBrowserHeight(int height)`
  - `getObsEffectSymbols() const` / `setObsEffectSymbols(const QString& symbols)`
  - `getObsEffectSize() const` / `setObsEffectSize(int size)`
  - `getObsEffectCount() const` / `setObsEffectCount(int count)`
  - `getObsEffectOpacity() const` / `setObsEffectOpacity(int opacity)`

#### `ITtsIntegration`
* 棒読みちゃんとVOICEVOXの連携を抽象化するインターフェースクラス。
* `virtual void initialize() = 0;` (自動起動などの初期化)
* `virtual void sendText(const QString& text) = 0;` (音声読み上げ要求)

#### `BouyomiIntegrationImpl`
* `ITtsIntegration` を継承する棒読みちゃん連携クラス。TCPソケット経由でのバイナリプロトコル送信を担当。
* 設定された速度（Speed）およびピッチ（Tone）のパラメータをバイナリデータ構造に組み込んで送信。
* 読み上げの際、渡されたユーザー名が `ConfigManager::getTtsIgnoreUsers()` または `ConfigManager::getBotUsers()` (自動Bot除外) に合致する場合はソケット送信を行わず早期リターンする。

#### `VoiceVoxIntegrationImpl`
* `ITtsIntegration` を継承するVOICEVOX連携クラス。
* `QNetworkAccessManager` を用いて、VOICEVOXローカルAPIに対して `/audio_query` を送信し、取得したクエリJSON内の `speedScale`、`pitchScale`、`volumeScale` などの値を `ConfigManager` の設定値で書き換え、`/synthesis` APIからWAV形式の音声データを受信。
* 受信したWAVデータを Windows Win32 API `PlaySound` を使ってメモリから非同期再生する。
* 読み上げ除外ユーザーの場合はAPIリクエストを行わず早期リターンする。

#### `ObsHttpServer`
* **機能**: OBS向けのHTML/JS/CSSファイルを配信する超軽量HTTPサーバー。
* **セキュリティ対策（3層の防御壁）**:
  1. **URLパス文字フィルタ**: リクエストされたURL（URLデコード前）が正規表現 `^[a-zA-Z0-9_\-\.\/]+$` にマッチするか検証。スペース、`%`、`:`、`\`、およびドット連続 `..` が含まれる場合はファイルアクセスを行わずに即座に `400 Bad Request` または `404 Not Found` を返す。
  2. **物理パス絶対範囲検証**: ドキュメントルート（`assets/overlay`）とリクエストされた相対パスを連結し、`QFileInfo::canonicalFilePath()` で物理実絶対パスを算出。これがドキュメントルートの物理実絶対パスで始まっていること（`startsWith`）を検証し、違反している場合は `403 Forbidden` を返す。
  3. **CSP（Content Security Policy）ヘッダーの強制適用**: 送出するHTTPヘッダーに以下を設定：
     `Content-Security-Policy: default-src 'self' http://localhost:* ws://localhost:* http://127.0.0.1:* ws://127.0.0.1:* ws: wss:; img-src 'self' data: https://static-cdn.jtvnw.net; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline';`

#### `ObsWebSocketServer`
* **機能**: コメントイベントおよび管理画面からの座標同期イベントをブロードキャストする中継サーバー。
* **メッセージリレー処理**:
  - `processTextMessage(const QString& message)` において、受信したJSON形式のテキストメッセージを解析。
  - アクションが `drag_start`, `drag_move`, `drag_end` などの管理側からの座標データである場合、**送信元のソケットを除く**すべての接続中のWebSocketクライアントへ無加工でデータをブロードキャスト転送する。

---

## 5. 処理シーケンス (データフロー詳細)

### 5.1 アプリケーション起動とセキュリティ検証
1. `main.cpp` 起動 → `TrustChain::Core::verifyToken()` 実行。不正時はプロセス強制終了。
2. `MainWindow` インスタンス生成 → `applyWatermark()` 適用。
3. `ConfigManager::loadConfig()` および `ConfigManager::loadToken()` で設定とトークンをロード。

### 5.2 コメント受信と読み上げ（TTS除外フィルター含む）
1. `TwitchEventCollectorImpl` がWebSocketからチャットメッセージを受信。
2. JSONをパースし、`CommentEvent` を構築して `AppController::onCommentReceived` へ `invokeMethod(Qt::QueuedConnection)` で通知。
3. `AppController` は非同期で `CommentAnalyzer`（解析）と `DatabaseManager`（DB保存）に処理をディスパッチ。
4. `AppController` は `ConfigManager` を用いて読み上げ除外判定を行う。
   - 発言者が `ttsIgnoreUsers` または `botUsers` に含まれる場合: 読み上げ処理をスキップ。
   - 含まれない場合: 現在有効な `ITtsIntegration`（`BouyomiIntegrationImpl` または `VoiceVoxIntegrationImpl`）の `sendText()` を呼び出して音声読み上げ要求を送出。
5. UIスレッドで `MainWindow::addCommentToView()` を呼び出して表示を更新。

### 5.3 チャットピン留めシーケンス
1. ユーザーがコメント表示行を右クリックし、「📌 チャット上部にピン留めする」から時間（例: 5分）を選択。
2. `MainWindow` は `m_controller` に向けて `onPinCommentRequested(messageId, 300)` を `invokeMethod` で通知。
3. `AppController` はワーカースレッドの `TwitchEventCollectorImpl::pinChatMessage()` を呼び出す。
4. `TwitchEventCollectorImpl` は `POST /helix/chat/pins?broadcaster_id=xxx&moderator_id=xxx` を実行（JSON: `{"message_id":"...","duration_seconds":300}`）。
5. 成功後、UIでピン留め中状態を記憶し、再度右クリックされた場合には「❌ ピン留めを解除」を表示。

### 5.4 チャットアナウンス送信シーケンス
1. ユーザーがコメントタブ最下部のアナウンス入力欄にテキストを入力し、プルダウンから背景色（例: `blue`）を選択して「アナウンス送信」をクリック。
2. `MainWindow` は `lineAnnouncementText` の内容を読み出し、`m_controller` へ `onSendAnnouncementRequested(message, color)` を `invokeMethod` で通知。
3. `AppController` は `TwitchEventCollectorImpl::sendAnnouncement(message, color)` を非同期に呼び出す。
4. `TwitchEventCollectorImpl` は、`POST /helix/chat/announcements` に対して以下のリクエストを送信する。
   - Query parameters: `broadcaster_id` (ストリーマーID), `moderator_id` (モデレーターID = ストリーマーID)
   - Header: `Authorization: Bearer <token>`, `Client-Id: <client_id>`, `Content-Type: application/json`
   - JSON Body: `{"message": "入力されたテキスト", "color": "blue"}`
5. APIレスポンス受信後、成功時は送信テキスト欄をクリアし、失敗時はレスポンスエラー内容をステータスバーまたはログへ通知する。

### 5.5 グラフの目盛り（Nice Steps）算出アルゴリズム
`MainWindow::updateChartDisplay()` における、画面サイズに適応した綺麗な目盛り間隔を求めるアルゴリズム。

#### X軸 (時間軸) のステップ算出
1. 表示領域の幅 `chartWidth` を取得し、目標分割数 `targetDivisionsX = max(2, chartWidth / 150)` を算出。
2. データの時間幅 `durationMs` を `targetDivisionsX` で割り、目安間隔 `targetIntervalX` を求める。
3. 規定の時間ステップ配列（5s, 10s, 30s, 1m, 2m, 5m, 10m, 15m, 30m, 1h...）の中から、`targetIntervalX` 以上の最小値となるステップ値 `niceIntervalX` を選択する。
4. X軸の表示範囲（最小値・最大値）を `niceIntervalX` の倍数に丸め、`tickCountX = (最大値 - 最小値) / niceIntervalX + 1` を設定する。

#### Y軸 (コメント数) のステップ算出
1. 表示領域の高さ `chartHeight` を取得し、目標分割数 `targetDivisionsY = max(2, chartHeight / 50)` を算出。
2. 最大コメント数 `maxCount` を `targetDivisionsY` で割り、目安ステップ `targetStep` を求める。
3. `targetStep` に応じて、以下のキリの良い値 `niceStep` を選択する。
   - `targetStep <= 1`: 1
   - `targetStep <= 2`: 2
   - `targetStep <= 5`: 5
   - `targetStep <= 10`: 10
   - `targetStep <= 20`: 20
   - `targetStep <= 25`: 25
   - `targetStep <= 50`: 50
   - `targetStep <= 100`: 100
   - `targetStep <= 250`: 250
   - `targetStep <= 500`: 500
   - それ以上は、10の累乗倍を基準に `10^k`, `2 * 10^k`, `2.5 * 10^k`, `5 * 10^k` を判定。
4. Y軸の表示範囲を `niceStep` の倍数に丸めて `[0, roundedMax]` とし、`tickCountY = roundedMax / niceStep + 1` を設定する。

### 5.6 物理アバター座標同期シーケンス
1. 管理画面（`physics/control.html`）上でアバターがドラッグ開始された際、WebSocket経由で `drag_start` メッセージを送信。
2. C++ `ObsWebSocketServer` が受信し、送信元以外の接続ソケット（表示画面 `physics/index.html`）へブロードキャスト。
3. 表示画面側は、対象アバターの `isDragging` フラグを `true` にし、物理落下・バウンド演算を一時サスペンドする。
4. 管理画面上でドラッグ移動中、境界枠左上からの相対座標をスケール値で割って実解像度座標 `(x, y)` を求め、WebSocket経由で `drag_move(x, y)` メッセージをリアルタイム送信。
5. C++ `ObsWebSocketServer` が転送し、表示画面側のアバターの座標 `(x, y)`（1:1解像度空間）を直接更新する（描画のみ同期）。
6. ドラッグが終了した際、WebSocket経由で `drag_end` メッセージを送信。
7. C++ `ObsWebSocketServer` が転送し、表示画面側は `isDragging` フラグを `false` に戻す。アバターはドロップされた現在座標を初速度ゼロの起点として、物理落下を再開する。

### 5.7 ダブルクリックエフェクト同期シーケンス
1. 管理画面（`physics/control.html`）上でアバターがダブルクリックされた際、WebSocket経由で `double_click` メッセージ（`{"action": "double_click", "userId": "ユーザー名"}`）を送信し、かつ管理画面ローカルでもパーティクルエフェクト関数 `createParticles` を呼び出す。
2. C++ `ObsWebSocketServer` が `double_click` メッセージを受信し、送信元以外の接続ソケット（表示画面 `physics/index.html`）へブロードキャスト。
3. 表示画面側はメッセージを受信し、該当アバターの現在座標中心 `(x + size/2, y + size/2)` から、設定同期されている `effectSymbols`, `effectSize`, `effectCount`, `effectOpacity` を用いて、四方に飛び散るフェードアウトパーティクルエフェクトを描画する。

---

## 6. OBS物理アバターオーバーレイアセット設計

`assets/overlay/` ディレクトリ内に配置するHTML/JS/CSSアセットの内部モジュール詳細設計。

### 6.1 物理表示画面 (`physics/index.html`)
* **環境自動検知と初期化シーケンス**:
  - `window.location.pathname.includes("/templates/custom/")` を判定基準とし、わんコメ環境で動作しているかを最速で検知する。
  - **わんコメ（OneSDK）モード**:
    - 動的に script タグを生成して `../__origin/js/onesdk.js` をロードし、ロード完了ハンドラで `OneSDK.setup()` を実行する。
    - `sdk.on('comment', ...)` イベントを受信し、コメントデータ（アバター画像URL含む）からアバターの出現・ジャンプ処理をトリガーする。
    - WebSocket サーバーへの再接続ループや接続処理は**一切開始しない（完全に抑止する）**。
  - **C++ツール連動モード**:
    - OneSDKの読み込みは一切行わず、404エラーも出力させない。
    - C++ツールが提供する WebSocket サーバー（`wsPort = window.location.port + 1`）に接続する。
    - WebSocket 経由で受信した `comment` アクションに基づき、アバター落下をトリガーし、コントロール画面（`control.html`）とのドラッグ座標同期や設定のリアルタイム同期を動作させる。
* **環境ごとの機能・仕様の自動分岐**:
  - **アバターサイズ（成長機能）**:
    - **わんコメモード**: コメントを重ねてもアバターサイズは成長（拡大）せず、`minSize` の固定値のまま維持される。
    - **C++モード**: コメントを重ねるごとにアバターの当たり判定サイズが段階的に成長（拡大）する：
      `size = Math.min(maxSize, minSize + (commentCount - 1) * 10)`
  - **スポーンX座標決定方式**:
    - **わんコメモード**: `userId` をシードとする決定論的ハッシュ乱数に基づいて出現X座標を固定分散配置する（配信主画面との同期を行わないため）。
    - **C++モード**: 出現時の重なりを回避するため、完全ランダム座標から既存アバターと衝突しない位置を探索する（`Math.random()` ベースの探索）。
  - **コメント時の演出（ジャンプ・パーティクル）**:
    - **わんコメモード**: 新着コメント受信時にアバターが上方向へピョコッとジャンプ（`vy = -jumpPower`）し、かつ自動的にパーティクルエフェクトが発生する。
    - **C++モード**: ジャンプや自動パーティクルは発生せず、物理落下のみ行われる（ダブルクリックイベントや同期イベントでのみパーティクルが発生する）。
* **構成**: 各アバターを `div`（アバター画像 `img` とコメント吹き出し `div` を内包）としてDOM動的生成・管理する構成とする。
* **物理ループ (自作 2D シミュレーション)**:
  - `requestAnimationFrame` にて `updatePhysics()` ループを毎フレーム呼び出し。
  - 各アバターオブジェクトのデータ構造：
    ```javascript
    {
      userId: "string",
      username: "string",
      avatarUrl: "string",
      element: HTMLElement,       // DOM要素への参照
      bubbleElement: HTMLElement, // 吹き出しDOM要素への参照
      x: number,                  // 現在位置 X (px)
      y: number,                  // 現在位置 Y (px)
      vx: number,                 // 水平速度 (px/frame)
      vy: number,                 // 垂直速度 (px/frame)
      size: number,               // 現在のサイズ (px)
      commentCount: number,       // 受信コメント数（成長判定用）
      isDragging: boolean,        // ドラッグ同期フラグ
      bubbleTimer: number         // 吹き出し消滅用タイマーID
    }
    ```
  - **物理挙動パラメータ**:
    - `gravity`: `0.5` px/frame²（重力加速度）
    - `bounceFactor`: C++設定値から取得した反発係数（設定値が `30` の場合 `e = 0.3`）
  - **更新処理**:
    - `isDragging == true` のアバターは、座標更新を物理シミュレーション対象から除外。
    - `isDragging == false` のアバターは、以下を実行：
      1. 重力適用: `vy += gravity`
      2. 座標更新: `y += vy`
      3. 地面衝突検知:
         `y + size >= windowHeight` に達した場合、`y = windowHeight - size` に位置修正し、速度を反転・減衰：`vy = -vy * (bounceFactor / 100)`。
         ただし、`Math.abs(vy) < 0.2` になった場合は `vy = 0` とし、微小なバウンドの繰り返しによるフリーズ/無駄な計算を防ぐ（静止状態へ移行）。
  - **直立固定**: CSS `transform: translate(x, y)` のみを用いてアバターを配置し、回転（`rotate`）を適用しないことで直立を維持する。
  - **ダブルクリック演出演出（パーティクル放出）**:
    - `createParticles(x, y)`: アバターの中心座標を中心に、設定値 `effectCount` 個の `div`（`.particle`）を生成。
    - 各要素に `effectSymbols` からランダムに選ばれた1文字を設定。
    - ランダムな進行方向角度（0〜2π）と移動量から目的地座標 `(--tx, --ty)` を算出してCSS変数としてインライン設定し、1秒経過後に自動的に `remove()` する。
    - CSSの `@keyframes fly-out` により、拡大しながら指定の目的地へ高速に飛んでフェードアウトする軽量なCSS3アニメーションを適用する。

### 6.2 プレビューおよびアバター成長ロジック
* **初期スポーン**:
  - 重複を防ぐため、`userId` をシードとする決定論的ハッシュ関数から擬似乱数 $r_p \in [0.0, 1.0)$ を算出する。
  - 初期配置座標 X を $x_{spawn} = (\text{windowWidth} - \text{size}) \cdot (0.2 + 0.6 \cdot r_p)$、Y座標を $0$、初期速度を $vx = 0$, $vy = 0$ としたアバターを出現させる。初期サイズは `ConfigManager` より取得した `obs_avatar_min_size`。
* **成長判定**: 同一ユーザーからのコメント受信ごとに `commentCount` をインクリメント。サイズを `minSize + (commentCount * 10)` で拡張（上限は `obs_avatar_max_size`）。サイズ変更に合わせてDOM要素の幅・高さ、および物理境界判定の `size` を動的に更新する。
* **吹き出し**: コメント受信時にアバターの頭上（`y - bubbleHeight`）に表示し（ユーザー名は含めず、メッセージ本文のみを表示）、7秒後にフェードアウトアニメーション（CSSクラス追加）を適用してDOMから削除する。アバターの移動・落下に同期して位置を追従させる。

### 6.3 セーフティガバナー（高負荷防止機構）
* **アバター数制限**: アバター配列の要素数が **30** を超えた場合、配列先頭（最も古いアバター）を取得し、DOMからフェードアウト削除した上で配列から除去する。
* **フレーム時間監視**:
  - `updatePhysics()` 内の処理開始から終了までの時間を `performance.now()` で計測。
  - 計算・描画処理時間が **10ms** を超えた場合、コンソールに警告ログを出力し、物理演算ループを一時停止（サスペンド状態）にする。再度安全な状態になるか、ユーザーによる管理画面からの操作が行われるまでサスペンド状態を維持し、ブラウザおよびOBSのフリーズを防止する。

### 6.2 管理操作画面 (`physics/control.html`)
* **構成と境界スケーリング**:
  - 設定された解像度（`obsBrowserWidth`・`obsBrowserHeight`）に基づき、管理用ウィンドウサイズ内にアスペクト比維持で収まる最大縮小スケール `scale = Math.min(availW / obsBrowserWidth, availH / obsBrowserHeight)` を計算。
  - 中央配置された境界枠（`#bounds`）を可視化表示し、アバター要素は境界枠内の座標でドラッグ移動を制限する（境界判定）。
  - **重複防止スポーン**: 表示画面と同様に `userId` をシードとする同一のハッシュベース擬似乱数 $r_p$ を用いて、初期スポーン座標 X を $x_{spawn} = (obsBrowserWidth - size) \cdot (0.2 + 0.6 \cdot r_p)$、Y座標を $50$ とした配置を行う。これにより、OBSと操作画面間で寸分違わない初期配置が同期される。
* **ドラッグ＆ドロップ実装**:
  - `mousedown` / `mousemove` / `mouseup` を用いて境界枠内でアバター要素をドラッグ可能にする。
  - ドラッグ開始時: WebSocket経由で `{ "action": "drag_start", "userId": "..." }` を送信。
  - ドラッグ移動中: 境界枠の左上からの相対座標を算出し、`scale` で割った1:1実座標 `(x, y)` を計算して `{ "action": "drag_move", "userId": "...", "x": x, "y": y }` を送信。
  - ドラッグ終了時: `{ "action": "drag_end", "userId": "..." }` を送信。
* **ダブルクリック演出**:
  - アバター要素に `dblclick` リスナーを登録。ダブルクリック時にローカルで `createParticles` を呼び出すとともに、WebSocket経由で `{ "action": "double_click", "userId": "..." }` をブロードキャストする。
