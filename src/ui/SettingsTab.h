#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QMap>
#include <QLineEdit>
#include <QSpinBox>
#include <QListWidget>
#include <QComboBox>
#include <QRadioButton>
#include <QPushButton>
#include <QLabel>
#include <QList>
#include "core/AppController.h"

namespace Ui {
class SettingsTab;
}

struct PluginSettingsItem {
    QString filePath;
    QString name;
    QString version;
    QString description;
    QByteArray iconPng;
    bool enabled;
};

class SettingsTab : public QWidget {
    Q_OBJECT
public:
    explicit SettingsTab(AppController* controller, QWidget* parent = nullptr);
    ~SettingsTab();

    void loadSettings();
    void saveSettings();

signals:
    void settingsSaved();

private slots:
    void onSaveButtonClicked();
    void onTestConnectionClicked();
    void onCopyUrlClicked();
    void onMoveUpClicked();
    void onMoveDownClicked();
    void onPluginSelectionChanged();
    void onTtsEngineToggled();
    void onTwitchTokenRetrieved(const QString& retrievedToken);
    void onTwitchConnectionStatusChanged(bool connected, const QString& accountName);

private:
    void setupUiManual();
    void refreshPluginList();
    QByteArray encryptToken(const QByteArray& token);
    QByteArray decryptToken(const QByteArray& token);
    void populateObsUrls();

    Ui::SettingsTab* ui;
    AppController* m_controller;

    // UI要素をプログラムから管理
    QLineEdit* m_twitchChannelEdit;
    QLineEdit* m_twitchTokenEdit;
    QPushButton* m_twitchTestBtn;
    QLabel* m_twitchStatusLabel;

    QSpinBox* m_obsPortSpin;
    QComboBox* m_obsUrlCombo;
    QPushButton* m_obsCopyBtn;

    QRadioButton* m_ttsBouyomiRadio;
    QRadioButton* m_ttsVoiceVoxRadio;
    QLineEdit* m_ttsHostEdit;
    QSpinBox* m_ttsPortSpin;

    QListWidget* m_pluginListWidget;
    QPushButton* m_pluginUpBtn;
    QPushButton* m_pluginDownBtn;
    
    QLabel* m_pluginNameVal;
    QLabel* m_pluginVerVal;
    QLabel* m_pluginDescVal;
    QLabel* m_pluginPathVal;

    QPushButton* m_saveBtn;

    QList<PluginSettingsItem> m_pluginItems;
};
