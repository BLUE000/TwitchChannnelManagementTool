#pragma once
#include <QMainWindow>
#include <QListWidget>
#include <QStackedWidget>
#include <QLabel>
#include "core/AppController.h"
#include "ui/SettingsTab.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppController* controller, QWidget* parent = nullptr);
    ~MainWindow() override;

    void applyTheme();
    void syncPluginTabs();

protected:
    void closeEvent(QCloseEvent* event) override;

private slots:
    void onSidebarSelectionChanged();
    void onSettingsSaved();
    void updateStatusBarTwitch(bool connected, const QString& accountName);

private:
    void setupUiManual();
    void restoreWindowState();
    void saveWindowState();

    Ui::MainWindow* ui;
    AppController* m_controller;
    
    // UI 要素
    QListWidget* m_sidebar;
    QStackedWidget* m_stackedWidget;
    SettingsTab* m_settingsTab;

    // ステータスバー表示用
    QLabel* m_statusTwitch;
    QLabel* m_statusObs;
    QLabel* m_statusTts;

    struct TabMapping {
        QString filePath; // プラグインのパス (空欄の場合は本体設定タブ)
        QWidget* widget;
    };
    QList<TabMapping> m_tabs;
};
