# Twitch OAuth スコープ定義書 (ドラフト)

本ドキュメントは、TwitchChannelManagementToolがTwitch APIおよびEventSub（WebSocket）と連携する際に、ログイン認証（OAuth）で要求する権限スコープを整理・定義するものです。

---

## 1. 要求スコープ一覧

| 機能エリア | スコープ名 (Scope) | 用途・説明 | 備考 |
| :--- | :--- | :--- | :--- |
| **チャット基本** | `chat:read` | チャットメッセージの受信 (IRC経由等) | 読み上げ・表示に必須 |
| | `chat:edit` | チャットメッセージの送信 (IRC経由等) | 時報やフォロー感謝メッセージの投稿に必須 |
| **新EventSubチャット** | `user:read:chat` | ユーザーとしてのチャット購読・受信 | Twitch API移行後の新仕様対応 |
| | `user:write:chat` | ユーザーとしてのチャット送信 | Twitch API移行後の新仕様対応 |
| **チャンネルポイント** | `channel:read:redemptions` | チャンネルポイント引き換えイベントの受信 | 演出トリガーの検知に必須 |
| | `channel:manage:redemptions` | チャンネルポイント引き換えステータス（承認/却下）の更新 | 演出完了後に自動的に「完了」にする場合に使用 |
| **フォロワー管理** | `moderator:read:followers` | フォロワー一覧・新規フォローイベントの受信 | フォロワー同期および新規フォロー検知に必須 |
| **モデレーション** | `moderator:manage:chat_messages` | チャットメッセージの削除 | NGワードコメントの自動削除に必須 |
| | `moderator:manage:banned_users` | ユーザーのBAN・タイムアウト処理 | 荒らしユーザーの一時制限/永久追放に必須 |
| **チャンネル管理** | `channel:read:goals` | クリエイター目標設定（フォロワー目標等）の受信 | フォロワーゴールゲージの自動同期用 |
| | `channel:manage:broadcast` | 配信情報の編集（タイトル、カテゴリー等） | 将来的に配信情報の書き換えをサポートする場合に必要 |

---

## 2. EventSub (WebSocket) サブスクリプションタイプ

OAuthでトークンを取得後、EventSub経由でリアルタイム受信するために登録する主要なイベントタイプです。

* **チャット受信**: `channel.chat.message` (要 `user:read:chat`)
* **ポイント引き換え**: `channel.channel_points_custom_reward_redemption.add` (要 `channel:read:redemptions`)
* **新規フォロー**: `channel.follow` (要 `moderator:read:followers`)
* **モデレーションアクション**: `channel.moderator.action`（オプション、他モデレータの動き検知用）
