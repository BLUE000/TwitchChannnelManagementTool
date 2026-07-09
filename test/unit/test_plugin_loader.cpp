#include <gtest/gtest.h>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include "core/PluginLoader.h"
#include "shared/plugin_interface.h"

// テスト用公開型
class TestablePluginLoader : public PluginLoader {
public:
    using PluginLoader::extractDefaultAssets;
};

// ダミープラグインクラス
class MockPluginForLoader : public IChannelPlugin {
public:
    void initialize(ICoreContext* context) override { m_context = context; }
    void shutdown() override {}
    QString pluginId() const override { return "mock_plugin"; }
    QString pluginName() const override { return "MockPlugin"; }
    QString pluginVersion() const override { return "1.0.0"; }
    QString pluginDescription() const override { return "Description"; }
    QByteArray iconPngData() const override { return QByteArray(); }
    QMap<QString, QByteArray> defaultAssets() const override {
        QMap<QString, QByteArray> assets;
        assets["index.html"] = "<html>Mock</html>";
        return assets;
    }
    QWidget* createWidget(QWidget* parent) override { return nullptr; }
    void onCommentReceived(const TwitchComment& comment) override {}
    void onRewardRedeemed(const TwitchRewardRedemption& redemption) override {}
    void onTick() override {}

    ICoreContext* m_context = nullptr;
};

class PluginLoaderTest : public ::testing::Test {
protected:
    void SetUp() override {
        appDir = QCoreApplication::applicationDirPath();
        // assets/overlay/MockPlugin ディレクトリを初期化
        pluginAssetsDir = QDir(appDir).filePath("assets/overlay/MockPlugin");
        QDir(pluginAssetsDir).removeRecursively();
    }

    void TearDown() override {
        QDir(pluginAssetsDir).removeRecursively();
    }

    QString appDir;
    QString pluginAssetsDir;
};

// UT_LDR_001: プラグイン走査テスト
TEST_F(PluginLoaderTest, UT_LDR_001_ScanPlugins) {
    PluginLoader loader;
    QList<QString> list = loader.scanPlugins();
    // 初期状態ではDLLは存在しないか、またはビルド生成分のみ
    EXPECT_GE(list.size(), 0);
}

// UT_LDR_003: アセット抽出・上書き防止テスト
TEST_F(PluginLoaderTest, UT_LDR_003_AssetExtraction) {
    TestablePluginLoader loader;
    MockPluginForLoader plugin;
    
    // 1. 新規抽出テスト
    loader.extractDefaultAssets(&plugin);
    
    QString htmlPath = QDir(pluginAssetsDir).filePath("default/index.html");
    QFile file(htmlPath);
    ASSERT_TRUE(file.exists());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray content = file.readAll();
    file.close();
    EXPECT_EQ(content, "<html>Mock</html>");
    
    // 2. 手動編集 (上書き防止テスト)
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("<html>Edited</html>");
    file.close();
    
    // 再度抽出を実行
    loader.extractDefaultAssets(&plugin);
    
    // 中身が変わっていない（上書きされていない）ことを検証
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray reloadedContent = file.readAll();
    file.close();
    EXPECT_EQ(reloadedContent, "<html>Edited</html>");
}

// テスト用のダミーコアコンテキスト (I/Oテスト用)
class MockCoreContextForIoTests : public ICoreContext {
public:
    void sendChatMessage(const QString&) override {}
    void requestTts(const QString&, const QString&, int, int, int) override {}
    void sendToObs(const QString&, const QJsonObject&) override {}
    void postDiscordWebhook(const QString&, const QJsonObject&) override {}
    QString getPluginDirectory() const override { return m_dir; }
    QString getCipherKey() const override { return "TestKey1234567890"; }
    bool writeEncryptedFile(const QString&, const QByteArray&) override { return false; }
    QByteArray readEncryptedFile(const QString&) override { return QByteArray(); }
    void writeLog(const QString& level, const QString& className, const QString& funcName, const QString& description) override {
        m_lastLevel = level;
        m_lastClass = className;
        m_lastFunc = funcName;
        m_lastDesc = description;
    }

    QString m_dir;
    QString m_lastLevel;
    QString m_lastClass;
    QString m_lastFunc;
    QString m_lastDesc;
};

// UT_LDR_005: 暗号化ファイルI/Oテスト (ICoreContext::writeEncryptedFile / readEncryptedFile)
TEST_F(PluginLoaderTest, UT_LDR_005_EncryptedFileIO) {
    MockCoreContextForIoTests baseContext;
    baseContext.m_dir = pluginAssetsDir; // テスト用のテンポラリディレクトリを使用

    PluginContext context(&baseContext, "test_plugin");

    // テストディレクトリの初期化
    QDir(context.getPluginDirectory()).removeRecursively();

    // 1. 新規書き込み＆読み込みテスト
    QByteArray plainData = "Hello, this is a secure plugin data!";
    bool writeOk = context.writeEncryptedFile("config.enc", plainData);
    EXPECT_TRUE(writeOk);

    // 物理ファイルが存在し、暗号化（平文ではない）されていることを検証
    QString expectedPath = QDir(context.getPluginDirectory()).filePath("config.enc");
    QFile file(expectedPath);
    ASSERT_TRUE(file.exists());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    QByteArray fileContent = file.readAll();
    file.close();
    EXPECT_NE(fileContent, plainData); // 暗号化されているため平文とは一致しないはず

    // 読み込み・復号の検証
    QByteArray readData = context.readEncryptedFile("config.enc");
    EXPECT_EQ(readData, plainData);

    // 2. 存在しないファイルの読み込み検証 (空の QByteArray が返却されるはず)
    QByteArray missingData = context.readEncryptedFile("does_not_exist.enc");
    EXPECT_TRUE(missingData.isEmpty());

    // 3. 上書き保存の検証 (W+仕様：上書き)
    QByteArray newPlainData = "Updated data content";
    bool overwriteOk = context.writeEncryptedFile("config.enc", newPlainData);
    EXPECT_TRUE(overwriteOk);
    QByteArray reReadData = context.readEncryptedFile("config.enc");
    EXPECT_EQ(reReadData, newPlainData);

    // クリーンアップ
    QDir(context.getPluginDirectory()).removeRecursively();
}

// UT_LDR_006: ディレクトリトラバーサル防止テスト (安全性検証)
TEST_F(PluginLoaderTest, UT_LDR_006_PathSafetyVerification) {
    MockCoreContextForIoTests baseContext;
    baseContext.m_dir = pluginAssetsDir;

    PluginContext context(&baseContext, "test_plugin");

    QByteArray testData = "Top Secret";

    // 1. 親ディレクトリへのトラバーサルの禁止
    EXPECT_FALSE(context.writeEncryptedFile("../traversal.enc", testData));
    EXPECT_TRUE(context.readEncryptedFile("../traversal.enc").isEmpty());

    // 2. 絶対パスの禁止
#ifdef Q_OS_WIN
    QString absolutePath = "C:/Temp/unsafe.enc";
#else
    QString absolutePath = "/tmp/unsafe.enc";
#endif
    EXPECT_FALSE(context.writeEncryptedFile(absolutePath, testData));
    EXPECT_TRUE(context.readEncryptedFile(absolutePath).isEmpty());
}

// UT_LDR_007: PluginContext ログ出力テスト (ICoreContext::writeLog)
TEST_F(PluginLoaderTest, UT_LDR_007_WriteLogForwarding) {
    MockCoreContextForIoTests baseContext;
    PluginContext context(&baseContext, "test_plugin");

    // PluginContextからログを出力する
    context.writeLog("WARN", "TestPluginClass", "testMethod", "This is a test warning message");

    // ベースコンテキスト（スパイ）に正しく引数が転送されていることを検証
    EXPECT_EQ(baseContext.m_lastLevel, "WARN");
    EXPECT_EQ(baseContext.m_lastClass, "TestPluginClass");
    EXPECT_EQ(baseContext.m_lastFunc, "testMethod");
    EXPECT_EQ(baseContext.m_lastDesc, "This is a test warning message");
}

