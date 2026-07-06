# Twitchコメント解析ツール 単体試験仕様書 (Unit Test Specification)

本ドキュメントは「詳細設計書（`detailed_design.md`）」に対応する単体試験（Unit Test）の仕様を定義する。
個々のクラス・モジュールが提供するメソッドが、定義された仕様（境界値、異常系、フォーマット等）通りに動作することを検証する。

## 1. 試験アプローチ
* **自動テスト**: `GTest` (Google Test) を用い、クラスのパブリックメソッドに対する入出力を検証する。
* **網羅性**: 正常系だけでなく、境界値（空文字、長文、不正なトークン等）や例外・異常系に重きを置いて検証する。

## 2. 単体試験項目

### 2.1 データ管理・難読化 (`DatabaseManager` & `TransCipher-Dist`)
| 試験ID | 対象関数/処理 | 入力条件 | 期待する結果（判定基準） | 実装状況 |
|---|---|---|---|---|
| UT-DB-01 | 難読化・復元の可逆性 | `saveComment()` に平文のコメントデータを渡す。 | 1. DB上（`chat_logs`テーブル）には難読化されたBLOBが保存されていること。<br>2. `searchComments()` で取り出したデータが、入力した平文と完全一致すること。 | 実装済 |
| UT-DB-02 | スキーマ初期化 | 初回起動（DBファイルが存在しない状態） | `chat_logs`, `user_stats` のテーブルが指定されたスキーマで正しく生成されること。 | 実装済 |
| UT-DB-03 | 古いログの圧縮処理 | `compressOldLogs()` を実行（7日以上前のデータが存在する状態） | 7日以前のデータが `chat_logs` から削除され、集計データのみが `user_stats` に維持されること。 | 未実装 |

### 2.2 コメント解析 (`CommentAnalyzer`)
| 試験ID | 対象関数/処理 | 入力条件 | 期待する結果（判定基準） | 実装状況 |
|---|---|---|---|---|
| UT-ANL-01 | NGワード判定（正常系） | `analyzeText()` にNGワードリストに含まれない通常のテキストを渡す。 | 戻り値の `AnalysisResult.isSpam` が `false` であること。 | 未実装 |
| UT-ANL-02 | NGワード判定（異常系） | `analyzeText()` にNGワードを含むテキストを渡す。 | 戻り値の `AnalysisResult.isSpam` が `true` であること。 | 未実装 |
| UT-ANL-03 | 感情判定スコア | `analyzeText()` に極端な肯定文、否定文をそれぞれ渡す。 | 肯定文の `sentiment_score` がプラス、否定文がマイナスの範囲に収まること。 | 未実装 |

### 2.3 セキュリティ・設定管理 (`ConfigManager` & TrustChain連携)
| 試験ID | 対象関数/処理 | 入力条件 | 期待する結果（判定基準） | 実装状況 |
|---|---|---|---|---|
| UT-CFG-01 | セキュアトークン保存 | `saveToken()` にOAuthトークン文字列を渡す。 | `loadToken()` で正しく値が取り出せること。かつ、ディスク上の設定ファイル（ini等）に平文でトークンが保存されていないこと。 | 実装済 |
| UT-SEC-01 | ヌル文字汚染スキャン | TrustChain処理内で、わざと `\0` を含む不正なトークンを注入する。 | 不正な文字列として例外、または即時エラー（強制終了のシグナル）が返されること。 | 未実装 |

### 2.4 視聴者リスト制御・モデレーション・UIユーティリティ (`TwitchEventCollectorImpl` & UI関連ロジック)
| 試験ID | 対象関数/処理 | 入力条件 | 期待する結果（判定基準） | 実装状況 |
|---|---|---|---|---|
| UT-CHT-01 | 視聴者データパース | Twitch APIのレスポンス（JSON配列）をダミーで流し込む。 | `broadcaster`, `moderator`, `vip`, `none` バッジが正しく判定され、ロールごとの `ChattersEvent` データ（ChatterInfo型）にマージされること。 | 実装済 |
| UT-CHT-02 | ボットユーザーの除外判定 | 除外リスト（`bot_users`）に登録されたユーザーを含むリスト。 | `bot` ロールとして適切にマークされること。 | 実装済 |
| UT-CHT-03 | 手動更新クールダウン判定 | 前回取得からの経過時間を変えて有効/無効状態を判定する。 | 3分未満は無効（False）、3分以上は有効（True）が返されること。 | 実装済 |
| UT-CHT-04 | BAN・タイムアウトAPIリクエスト構築 | ターゲットユーザーIDと duration（タイムアウト時間）を指定して `banUser()` を呼ぶ。 | 1. タイムアウト時は JSON に `duration` を含める。<br>2. 永久BAN時（duration=0）は JSON に `duration` を含めないリクエストボディが構築されること。 | 未実装 |
| UT-CHT-05 | VIP・モデレータトグル操作 | 対象の現在の就任状態に応じたボタン押下。 | ONならDELETEメソッド、OFFならPOSTメソッドで対応するエンドポイントにリクエストが構築されること。 | 未実装 |
| UT-CHT-06 | シャウトアウトクールタイム制御 | シャウトアウト実行後の経過時間判定。 | 2分間（120秒）はボタンが無効化され、カウントダウン用タイマーが正常に動作すること。 | 未実装 |
| UT-CHT-07 | アナウンスAPIリクエスト構築 | 送信テキスト（"Hello"）と色（"blue"）を指定して `sendAnnouncement()` を呼ぶ。 | 1. リクエストURLの `broadcaster_id` および `moderator_id` クエリ値が正しいこと。<br>2. JSONボディに `message` と `color` が正しく格納されていること。 | 未実装 |
| UT-CHT-08 | グラフNice Step算出ロジック | 最大カウント値（例: 3, 27, 120）と描画高さ（例: 200, 300）を指定してステップ算出を行う。 | 割り切れる綺麗な整数間隔（1, 5, 20 など）がステップ値として選択され、Y軸の上限値がそのステップ値の倍数に丸められること。 | 未実装 |

### 2.5 読み上げ（TTS）制御
| 試験ID | 対象関数/処理 | 入力条件 | 期待する結果（判定基準） | 実装状況 |
|---|---|---|---|---|
| UT-TTS-01 | 読み上げ除外判定 | 除外対象ユーザー（Botまたは手動除外ユーザー）と通常ユーザーのチャットデータをそれぞれ渡す。 | 1. 除外対象ユーザーからのメッセージ時は、読み上げ送出がスキップ（送信処理なし）されること。<br>2. 通常ユーザーからのメッセージ時は、正しく送信処理（各エンジンのsendText）が走ること。 | 未実装 |
| UT-TTS-02 | ConfigManager拡張とシリアライズ | ConfigManagerにてactiveTtsEngineを切り替え、各種パラメータ（ホスト、ポート、音量、速度、音程）を設定して保存、再ロードする。 | 1. `config.json` に設定値が難読化されて保存されること。<br>2. 棒読みちゃんとVOICEVOXの設定（Host, Port, Exe, Voice/Speaker, Volume, Speed, Pitch）が個別に保持・復元されること。 | 未実装 |
| UT-TTS-03 | BouyomiIntegrationImpl バイナリ構築 | 速度（Speed）およびピッチ（Pitch）をスライダー設定から渡し、`sendText()` を呼び出す。 | 1. 棒読みちゃん向けTCPバイナリパケットの速度（Speed）および音程（Tone）のバイト領域が、設定された値（例: 速度100%, 音程100%）に正しくマッピングされて送信されること。 | 未実装 |
| UT-TTS-04 | VoiceVoxIntegrationImpl クエリ書換え | `/audio_query` から返却されるダミーJSONデータに対して、設定した音量（Volume）、速度（Speed）、音程（Pitch）を適用する。 | 1. JSON内の `speedScale`、`pitchScale`、`volumeScale` が指定された浮動小数点数（例: speedScale=1.2, pitchScale=0.05, volumeScale=0.8）に正しく書き換えられていること。 | 未実装 |
| UT-TTS-05 | VoiceVoxIntegrationImpl APIリクエスト | テキスト送信メソッドを実行する。 | 1. `/audio_query` をPOSTし、得られたレスポンスを `/synthesis` へPOSTするHTTPリクエストが正しく構築されること。 | 未実装 |

### 2.6 OBS連携・セキュリティ・物理オーバーレイ
| 試験ID | 対象関数/処理 | 入力条件 | 期待する結果（判定基準） | 実装状況 |
|---|---|---|---|---|
| UT-OBS-01 | ConfigManager物理設定保存 | ConfigManagerに物理設定値（MinSize=80, MaxSize=200, BounceFactor=45）を設定して `saveConfig()` を実行。 | `config.json` に正しく保存され、`loadConfig()` 後に同じ値が復元されること。 | 未実装 |
| UT-OBS-02 | ObsHttpServer URLフィルタ | リクエストパスに「スペース」「%」「:」「\」「..」などを含む不正なGETリクエストを送信する。 | ファイルオープン処理に入らず、即座に 400 Bad Request または 404 Not Found 応答を返すこと。 | 未実装 |
| UT-OBS-03 | ObsHttpServer 実パス範囲検証 | 相対パス解決後に実絶対パスが `assets/overlay/` ディレクトリ配下からはみ出るリクエスト（`/../CMakeLists.txt` 等）を送信する。 | 403 Forbidden 応答が返され、対象ファイルの内容が読み込まれないこと。 | 未実装 |
| UT-OBS-04 | ObsHttpServer CSPヘッダー付与 | 正常なファイルをリクエストする。 | レスポンスヘッダーに `Content-Security-Policy` が含まれ、自己ホスト、指定外部ドメインおよびWebSocket（`ws:`, `wss:`）接続が許可されていること。 | 未実装 |
| UT-OBS-05 | ObsWebSocketServer メッセージリレー | 接続されたソケットAから `{ "action": "drag_move", ... }` を送信する。 | 送信元ソケットAにはエコーバックされず、接続中の他のソケットBにのみデータが転送されること。 | 未実装 |
| UT-OBS-06 | ConfigManager追加設定保存 | ConfigManagerに解像度（Width=1024, Height=768）およびエフェクト（Symbols="★,✨", Size=25, Count=8）を設定して `saveConfig()` を実行。 | `config.json` に正しく保存され、`loadConfig()` 後に同じ値が復元されること。 | 未実装 |

## 3. テストの実行と判定基準
* すべてのテストケースは `ctest` コマンド等により一括実行可能であること。
* 単体テストのカバレッジは、コアロジック（`DatabaseManager`, `CommentAnalyzer` 等）において80%以上を目標とする。
