# Plugins Directory / プラグインフォルダ

本フォルダは、TwitchChannelManagementToolのプラグインモジュールを格納および管理するためのディレクトリです。

## ディレクトリ構成 (Directory Structure)

アプリケーション実行時、以下のような構成になるように配置・出力されます。

```text
plugins/
├── [PluginName].dll             <-- プラグインのDLL本体（直接この下に配置）
└── [PluginName]/                <-- 各プラグインのプライベートフォルダ（自動生成）
    ├── config.enc               <-- プラグイン固有の暗号化設定値
    └── logs/
        └── session_1.enc        <-- プラグイン固有の暗号化ログファイル等
```

### 1. プラグインのDLLファイル
ビルドしたプラグインの動的ライブラリ（Windows: `.dll`、Mac: `.dylib`、Linux: `.so`）は、**`plugins/` ディレクトリ直下**に直接配置してください。

例：
*   `plugins/CommentManagerPlugin.dll`

---

### 2. プラグイン固有フォルダ（プラグイン名と同名）
各プラグインがホスト側の暗号化ファイル I/O（`writeEncryptedFile`）を利用してデータを保存する際、この `plugins/[PluginId]/` 配下に暗号化されたデータが自動で書き出されます。

例：
*   `plugins/CommentManagerPlugin/`
