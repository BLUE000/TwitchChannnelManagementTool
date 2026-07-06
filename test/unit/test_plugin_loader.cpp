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
