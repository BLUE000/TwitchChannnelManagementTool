# TwitchChannelManagementTool 単体テスト仕様書 (ドラフト)

本ドキュメントは、詳細設計に対応する「単体テスト（Unit Test）」の設計および検証項目を定義するものです。Google Test (GTest) および Qt Test (QTest) を用いて自動化します。

---

## 1. 単体テスト方針
* **対象範囲**: 各モジュール・クラスの個別メソッドおよび内部ロジック。
* **テストフレームワーク**:
  - GUI操作・イベントループ・Qtのシグナル/スロットに関わるテスト: **Qt Test (QTest)**
  - 純粋なロジック、データ変換、ユーティリティ機能のテスト: **Google Test (GTest)**
* **ビルド構成**: CMakeの `BUILD_TESTING` オプションが `ON` の時のみテストターゲット（`run_unit_tests.exe`）をビルドします。

---

## 2. テストケース一覧

### 2.1. Logger モジュール単体テスト (`src/modules/logger/`)
* **UT_LOG_001: ログ有効判定テスト (`debug_config.json` 読み込み)**
  - **検証内容**:
    1. テスト実行ディレクトリに `debug_config.json` が存在しない、または `{"enable_logging": false}` であるとき、`Logger::log()` がファイルを生成せず早期リターンすること。
    2. `{"enable_logging": true}` の平文設定ファイルが存在するとき、`Logger::log()` がログ書き出し処理に入ること。
* **UT_LOG_002: 難読化およびファイル書き出しテスト**
  - **検証内容**:
    - `Logger::log()` を呼び出した際、指定フォーマット `[YYYY-MM][DD-HH-mm-ss][Action][Class][Func][Description]` の平文ログが生成され、TransCipherで難読化（Base64形式）されてファイルに1行ずつ正しく追記されること。
    - 追記されたデータをテスト内でデコード・復号した際、元の平文ログメッセージと完全一致すること。

### 2.2. プラグインローダー単体テスト (`src/core/`)
* **UT_LDR_001: プラグイン走査テスト (`PluginLoader::scanPlugins()`)**
  - **検証内容**: 指定されたテスト用プラグインフォルダ内にダミーDLLを配置し、正しいDLL名およびパスがスキャン結果（リスト）に返ることを確認する。
* **UT_LDR_002: 署名検証およびロード成否テスト**
  - **検証内容**:
    - 正規の TrustChain 署名を持つダミープラグインが正常にロードされ、`IChannelPlugin` インターフェースにキャスト可能であること。
    - 署名がない、または破損している不正なDLLが、ロードされずスキップされること。
* **UT_LDR_003: アセット抽出・上書き防止テスト**
  - **検証内容**:
    1. 対象の書き出しパス `assets/overlay/[プラグイン名]/default/` にファイルが存在しない場合、`defaultAssets()` が返したバイナリデータがファイルとして新規保存されること。
    2. すでにファイルが存在する場合、新規書き出し（上書き）がスキップされ、既存のファイルの中身がそのまま維持されること。
* **UT_LDR_004: アイコン画像変換テスト**
  - **検証内容**:
    - プラグインが返した `iconPngData()` の `QByteArray` から `QPixmap::loadFromData` を用いて、破損なく正常に `QPixmap` オブジェクトがデコード・生成できることを確認する。
* **UT_LDR_005: 暗号化ファイルI/Oテスト (ICoreContext::writeEncryptedFile / readEncryptedFile)**
  - **検証内容**:
    1. プラグインコンテキストに対し、相対ファイル名（例: `"config.json"`, `"sub/data.txt"`) と任意の平文データを指定して `writeEncryptedFile()` を呼び出した際、ファイルが自動生成され、中身が TransCipher によって正しく暗号化されて保存されること。
    2. 保存されたファイルに対して `readEncryptedFile()` を呼び出した際、自動で復号が行われ、元の平文データと完全一致するデータが取得できること。
    3. 存在しないファイルパスを指定して `readEncryptedFile()` を呼び出した際、エラーや例外を起こさずに安全に空の `QByteArray()` が返却されること。
    4. 既存のファイルに対して再度 `writeEncryptedFile()` を呼び出した際、以前のデータがクリアされて上書き保存（PHPでの `fopen("w")` と同等）されること。
* **UT_LDR_006: ディレクトリトラバーサル防止テスト (パス安全性検証)**
  - **検証内容**:
    1. `writeEncryptedFile()` および `readEncryptedFile()` の相対パス引数に、`"../outside.txt"` のように親ディレクトリを参照するパスや、絶対パス（例: `"C:/Windows/system32/cmd.exe"`, `"/etc/passwd"`) を指定した際、ホスト側で不正アクセスとして安全に処理が拒否されること（書き込み時は `false` を返し、読み込み時は空データを返す）。
    2. これにより、プラグインが割り当てられた固有ディレクトリ（`plugins/[PluginId]/`）の外部にあるファイルに対してアクセス・改ざんできないセキュアな設計になっていること。


### 2.3. TTS連携モジュール単体テスト (`src/modules/tts/`)
* **UT_TTS_001: BouyomiClient パケット生成テスト**
  - **検証内容**:
    - 棒読みちゃん用クライアントにテキスト、速度、ピッチ、音量等を指定して `speak()` を呼んだ際、棒読みちゃんのソケット通信仕様（16バイト＋可変長データ）のバイナリバッファ（ヘッダー、速度、ピッチ、音量、テキストエンコーディング等）が規格通りにバイト生成されることをインメモリバッファ検証で確認する。
* **UT_TTS_002: VoiceVoxClient クエリパラメータ書き換えテスト**
  - **検証内容**:
    - VOICEVOX用クライアントにおいて、モックされたHTTPレスポンスから取得したJSONオブジェクト（audio_query結果）に対し、設定値通りの `speedScale`, `pitchScale`, `volumeScale` が正しく書き換えられることを確認する。

### 2.4. OBS連携モジュール単体テスト (`src/modules/obs/`)
* **UT_OBS_001: WebSocket 送信JSONフォーマットテスト**
  - **検証内容**:
    - `ObsHttpWebSocketServer::broadcastToObs("comments", data)` を呼び出した際、ブロードキャストされる文字列が、指定したJSONフォーマット（汎用コメント配信仕様：`type: "comments"`）に厳格に準拠しているかを確認する。
* **UT_OBS_002: Discord Webhook ペイロードシリアライズテスト**
  - **検証内容**:
    - `postDiscordWebhook` 用に構築された `payload`（`QJsonObject`）が、正しい JSON フォーマットにシリアライズされ、HTTP 送信用の `QByteArray` ボディデータへ正確に変換されることを確認する。

