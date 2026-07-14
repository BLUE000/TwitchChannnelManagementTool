#include "SettingsTab.h"
#include "ui_SettingsTab.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QClipboard>
#include <QGuiApplication>
#include <QNetworkInterface>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QMessageBox>
#include <QPluginLoader>
#include "logger/Logger.h"
#include "cipher_engine.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

SettingsTab::SettingsTab(AppController* controller, QWidget* parent)
    : QWidget(parent)
    , ui(new Ui::SettingsTab)
    , m_controller(controller)
{
    ui->setupUi(this);
    setupUiManual();
    
    // イベントバインド
    connect(m_saveBtn, &QPushButton::clicked, this, &SettingsTab::onSaveButtonClicked);
    connect(m_twitchTestBtn, &QPushButton::clicked, this, &SettingsTab::onTestConnectionClicked);
    connect(m_obsCopyBtn, &QPushButton::clicked, this, &SettingsTab::onCopyUrlClicked);
    connect(m_pluginUpBtn, &QPushButton::clicked, this, &SettingsTab::onMoveUpClicked);
    connect(m_pluginDownBtn, &QPushButton::clicked, this, &SettingsTab::onMoveDownClicked);
    connect(m_pluginListWidget, &QListWidget::itemSelectionChanged, this, &SettingsTab::onPluginSelectionChanged);
    
    connect(m_ttsBouyomiRadio, &QRadioButton::toggled, this, &SettingsTab::onTtsEngineToggled);
    connect(m_ttsVoiceVoxRadio, &QRadioButton::toggled, this, &SettingsTab::onTtsEngineToggled);
    
    connect(m_obsPortSpin, &QSpinBox::valueChanged, this, &SettingsTab::populateObsUrls);
    
    // Twitch イベントコレクターのシグナルバインド (一括バインドして重複接続を防ぐ)
    connect(m_controller->twitchCollector(), &TwitchEventCollector::tokenRetrieved, this, &SettingsTab::onTwitchTokenRetrieved);
    connect(m_controller->twitchCollector(), &TwitchEventCollector::connectionStatusChanged, this, &SettingsTab::onTwitchConnectionStatusChanged);

    loadSettings();
}

SettingsTab::~SettingsTab() {
    delete ui;
}

void SettingsTab::setupUiManual() {
    // スクロールエリア
    QScrollArea* scrollArea = new QScrollArea(this);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);
    
    QWidget* scrollContent = new QWidget(scrollArea);
    QVBoxLayout* mainLayout = new QVBoxLayout(scrollContent);
    mainLayout->setContentsMargins(15, 15, 15, 15);
    mainLayout->setSpacing(15);

    // 1. Twitch 連携設定
    QGroupBox* twitchGroup = new QGroupBox("Twitch 連携設定", scrollContent);
    QFormLayout* twitchForm = new QFormLayout(twitchGroup);
    m_twitchChannelEdit = new QLineEdit(twitchGroup);
    m_twitchChannelEdit->setPlaceholderText("接続先のチャンネルIDを入力");
    
    m_twitchTokenEdit = new QLineEdit(twitchGroup);
    m_twitchTokenEdit->setEchoMode(QLineEdit::Password);
    m_twitchTokenEdit->setPlaceholderText("oauth:xxxxxxxxxxxxxxxxxxxxxxxxxxxxxx (空欄時はブラウザで認証)");
    
    QHBoxLayout* twitchBtnLayout = new QHBoxLayout();
    m_twitchTestBtn = new QPushButton("接続テスト/認証開始", twitchGroup);
    m_twitchStatusLabel = new QLabel("🔴 未接続", twitchGroup);
    twitchBtnLayout->addWidget(m_twitchTestBtn);
    twitchBtnLayout->addWidget(m_twitchStatusLabel);
    twitchBtnLayout->addStretch();

    twitchForm->addRow("チャンネルID:", m_twitchChannelEdit);
    twitchForm->addRow("OAuthトークン:", m_twitchTokenEdit);
    twitchForm->addRow("", twitchBtnLayout);
    mainLayout->addWidget(twitchGroup);

    // 2. OBS 連携設定
    QGroupBox* obsGroup = new QGroupBox("OBS 連携設定 (HTTP/WebSocket)", scrollContent);
    QFormLayout* obsForm = new QFormLayout(obsGroup);
    m_obsPortSpin = new QSpinBox(obsGroup);
    m_obsPortSpin->setRange(1024, 65535);
    m_obsPortSpin->setValue(58081);

    QHBoxLayout* obsCopyLayout = new QHBoxLayout();
    m_obsUrlCombo = new QComboBox(obsGroup);
    m_obsCopyBtn = new QPushButton("コピー", obsGroup);
    obsCopyLayout->addWidget(m_obsUrlCombo, 1);
    obsCopyLayout->addWidget(m_obsCopyBtn);

    obsForm->addRow("サーバーポート:", m_obsPortSpin);
    obsForm->addRow("ブラウザソース用URL:", obsCopyLayout);
    mainLayout->addWidget(obsGroup);

    // 3. 音声合成 (TTS) 連携設定
    QGroupBox* ttsGroup = new QGroupBox("音声合成 (TTS) 連携設定", scrollContent);
    QFormLayout* ttsForm = new QFormLayout(ttsGroup);
    
    QHBoxLayout* ttsRadioLayout = new QHBoxLayout();
    m_ttsBouyomiRadio = new QRadioButton("棒読みちゃん", ttsGroup);
    m_ttsVoiceVoxRadio = new QRadioButton("VOICEVOX", ttsGroup);
    m_ttsBouyomiRadio->setChecked(true);
    ttsRadioLayout->addWidget(m_ttsBouyomiRadio);
    ttsRadioLayout->addWidget(m_ttsVoiceVoxRadio);
    ttsRadioLayout->addStretch();

    m_ttsHostEdit = new QLineEdit("localhost", ttsGroup);
    m_ttsPortSpin = new QSpinBox(ttsGroup);
    m_ttsPortSpin->setRange(1, 65535);
    m_ttsPortSpin->setValue(50001);

    ttsForm->addRow("連携ツール:", ttsRadioLayout);
    ttsForm->addRow("ホスト名:", m_ttsHostEdit);
    ttsForm->addRow("ポート番号:", m_ttsPortSpin);
    mainLayout->addWidget(ttsGroup);

    // 4. プラグイン管理設定
    QGroupBox* pluginGroup = new QGroupBox("プラグイン管理設定", scrollContent);
    QVBoxLayout* pluginVLayout = new QVBoxLayout(pluginGroup);
    
    QHBoxLayout* pluginHLayout = new QHBoxLayout();
    m_pluginListWidget = new QListWidget(pluginGroup);
    m_pluginListWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    
    QVBoxLayout* pluginBtnLayout = new QVBoxLayout();
    m_pluginUpBtn = new QPushButton("上へ", pluginGroup);
    m_pluginDownBtn = new QPushButton("下へ", pluginGroup);
    pluginBtnLayout->addWidget(m_pluginUpBtn);
    pluginBtnLayout->addWidget(m_pluginDownBtn);
    pluginBtnLayout->addStretch();

    pluginHLayout->addWidget(m_pluginListWidget, 1);
    pluginHLayout->addLayout(pluginBtnLayout);
    pluginVLayout->addLayout(pluginHLayout);

    // 詳細情報
    QGroupBox* detailGroup = new QGroupBox("プラグイン詳細情報", pluginGroup);
    QFormLayout* detailForm = new QFormLayout(detailGroup);
    m_pluginNameVal = new QLabel("-", detailGroup);
    m_pluginVerVal = new QLabel("-", detailGroup);
    m_pluginDescVal = new QLabel("-", detailGroup);
    m_pluginDescVal->setWordWrap(true);
    m_pluginPathVal = new QLabel("-", detailGroup);
    m_pluginPathVal->setWordWrap(true);
    
    detailForm->addRow("名前:", m_pluginNameVal);
    detailForm->addRow("バージョン:", m_pluginVerVal);
    detailForm->addRow("説明:", m_pluginDescVal);
    detailForm->addRow("パス:", m_pluginPathVal);
    
    pluginVLayout->addWidget(detailGroup);
    mainLayout->addWidget(pluginGroup);

    // 5. 保存ボタンエリア
    QHBoxLayout* saveBtnLayout = new QHBoxLayout();
    m_saveBtn = new QPushButton("設定保存", scrollContent);
    m_saveBtn->setMinimumSize(120, 35);
    saveBtnLayout->addStretch();
    saveBtnLayout->addWidget(m_saveBtn);
    mainLayout->addLayout(saveBtnLayout);

    scrollArea->setWidget(scrollContent);
    
    QVBoxLayout* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->addWidget(scrollArea);

    populateObsUrls();
}

void SettingsTab::populateObsUrls() {
    if (!m_obsUrlCombo) return;
    m_obsUrlCombo->clear();
    int port = m_obsPortSpin->value();
    m_obsUrlCombo->addItem(QString("http://localhost:%1/").arg(port));
    
    QList<QHostAddress> list = QNetworkInterface::allAddresses();
    for (const QHostAddress& addr : list) {
        if (addr.protocol() == QAbstractSocket::IPv4Protocol && !addr.isLoopback()) {
            m_obsUrlCombo->addItem(QString("http://%1:%2/").arg(addr.toString()).arg(port));
        }
    }
}

QByteArray SettingsTab::encryptToken(const QByteArray& token) {
#ifdef Q_OS_WIN
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(token.constData()));
    input.cbData = token.size();
    
    DATA_BLOB output;
    if (CryptProtectData(&input, L"TCMTSettings", NULL, NULL, NULL, 0, &output)) {
        QByteArray result(reinterpret_cast<char*>(output.pbData), output.cbData);
        LocalFree(output.pbData);
        return result;
    }
#endif
    CipherResult res = CipherEngine::encrypt(token, "TCMT_Settings_BackupKey");
    if (res.isSuccess()) {
        return res.data();
    }
    return token;
}

QByteArray SettingsTab::decryptToken(const QByteArray& token) {
    if (token.isEmpty()) return token;
#ifdef Q_OS_WIN
    if (token.startsWith("TCF")) {
        CipherResult res = CipherEngine::decrypt(token, "TCMT_Settings_BackupKey");
        if (res.isSuccess()) return res.data();
    } else {
        DATA_BLOB input;
        input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(token.constData()));
        input.cbData = token.size();
        
        DATA_BLOB output;
        if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output)) {
            QByteArray result(reinterpret_cast<char*>(output.pbData), output.cbData);
            LocalFree(output.pbData);
            return result;
        }
    }
#endif
    if (token.startsWith("TCF")) {
        CipherResult res = CipherEngine::decrypt(token, "TCMT_Settings_BackupKey");
        if (res.isSuccess()) return res.data();
    }
    return token;
}

void SettingsTab::loadSettings() {
    QString appDir = QCoreApplication::applicationDirPath();
    QString settingsPath = QDir(appDir).filePath("settings.bin");
    
    QFile file(settingsPath);
    QJsonObject settingsObj;
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray encryptedData = file.readAll();
        file.close();
        
        QByteArray rawJson = decryptToken(encryptedData);
        QJsonDocument doc = QJsonDocument::fromJson(rawJson);
        if (!doc.isNull() && doc.isObject()) {
            settingsObj = doc.object();
        }
    }
    
    // UIへの展開
    m_twitchChannelEdit->setText(settingsObj.value("twitch_channel").toString(""));
    m_twitchTokenEdit->setText(settingsObj.value("twitch_token").toString(""));
    m_obsPortSpin->setValue(settingsObj.value("obs_port").toInt(58081));
    
    int ttsEngine = settingsObj.value("tts_engine").toInt(0);
    if (ttsEngine == 1) {
        m_ttsVoiceVoxRadio->setChecked(true);
        m_ttsPortSpin->setValue(settingsObj.value("tts_port").toInt(50021));
    } else {
        m_ttsBouyomiRadio->setChecked(true);
        m_ttsPortSpin->setValue(settingsObj.value("tts_port").toInt(50001));
    }
    m_ttsHostEdit->setText(settingsObj.value("tts_host").toString("localhost"));

    // プラグイン一覧の構築
    refreshPluginList();
    
    // 保存されている有効・無効化状態と表示順序の適用
    QJsonArray pluginOrder = settingsObj.value("plugin_order").toArray();
    QMap<QString, int> orderMap;
    for (int i = 0; i < pluginOrder.size(); ++i) {
        orderMap[pluginOrder[i].toString()] = i;
    }
    
    QJsonArray enabledPlugins = settingsObj.value("enabled_plugins").toArray();
    QSet<QString> enabledSet;
    for (int i = 0; i < enabledPlugins.size(); ++i) {
        enabledSet.insert(enabledPlugins[i].toString());
    }
    
    // m_pluginItems の有効フラグと順序を再配置
    for (PluginSettingsItem& item : m_pluginItems) {
        QFileInfo fi(item.filePath);
        QString filename = fi.fileName();
        item.enabled = enabledSet.contains(filename);
    }
    
    // ソート
    std::sort(m_pluginItems.begin(), m_pluginItems.end(), [&orderMap](const PluginSettingsItem& a, const PluginSettingsItem& b) {
        QFileInfo fiA(a.filePath);
        QFileInfo fiB(b.filePath);
        int indexA = orderMap.contains(fiA.fileName()) ? orderMap[fiA.fileName()] : 9999;
        int indexB = orderMap.contains(fiB.fileName()) ? orderMap[fiB.fileName()] : 9999;
        return indexA < indexB;
    });
    
    // UIリストの描画
    m_pluginListWidget->clear();
    for (const PluginSettingsItem& item : m_pluginItems) {
        QListWidgetItem* listItem = new QListWidgetItem(m_pluginListWidget);
        QFileInfo fi(item.filePath);
        listItem->setText(QString("%1 (%2)").arg(item.name).arg(fi.fileName()));
        
        // アイコン
        if (!item.iconPng.isEmpty()) {
            QPixmap pix;
            if (pix.loadFromData(item.iconPng, "PNG")) {
                listItem->setIcon(QIcon(pix));
            }
        }
        
        listItem->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled | Qt::ItemIsSelectable);
        listItem->setCheckState(item.enabled ? Qt::Checked : Qt::Unchecked);
    }
    
    populateObsUrls();
}

void SettingsTab::saveSettings() {
    QJsonObject settingsObj;
    settingsObj["twitch_channel"] = m_twitchChannelEdit->text();
    settingsObj["twitch_token"] = m_twitchTokenEdit->text();
    settingsObj["obs_port"] = m_obsPortSpin->value();
    settingsObj["tts_engine"] = m_ttsBouyomiRadio->isChecked() ? 0 : 1;
    settingsObj["tts_host"] = m_ttsHostEdit->text();
    settingsObj["tts_port"] = m_ttsPortSpin->value();
    
    // リストの現在チェック状態を m_pluginItems へ同期
    QJsonArray enabledPlugins;
    QJsonArray pluginOrder;
    
    for (int i = 0; i < m_pluginListWidget->count(); ++i) {
        QListWidgetItem* listItem = m_pluginListWidget->item(i);
        bool checked = (listItem->checkState() == Qt::Checked);
        m_pluginItems[i].enabled = checked;
        
        QFileInfo fi(m_pluginItems[i].filePath);
        QString filename = fi.fileName();
        
        pluginOrder.append(filename);
        if (checked) {
            enabledPlugins.append(filename);
        }
    }
    
    settingsObj["enabled_plugins"] = enabledPlugins;
    settingsObj["plugin_order"] = pluginOrder;
    
    QJsonDocument doc(settingsObj);
    QByteArray rawJson = doc.toJson(QJsonDocument::Compact);
    QByteArray encrypted = encryptToken(rawJson);
    
    QString appDir = QCoreApplication::applicationDirPath();
    QString settingsPath = QDir(appDir).filePath("settings.bin");
    
    QFile file(settingsPath);
    if (file.open(QIODevice::WriteOnly)) {
        file.write(encrypted);
        file.close();
        Logger::instance().log("INFO", "SettingsTab", "saveSettings", "Settings saved successfully");
    } else {
        Logger::instance().log("ERROR", "SettingsTab", "saveSettings", "Failed to write settings file");
    }
}

void SettingsTab::refreshPluginList() {
    m_pluginItems.clear();
    QList<QString> list = m_controller->pluginLoader()->scanPlugins();
    
    for (const QString& filePath : list) {
        // 詳細情報抽出のため一時的にロード
        QPluginLoader loader(filePath);
        QObject* instance = loader.instance();
        if (instance) {
            IChannelPlugin* plugin = qobject_cast<IChannelPlugin*>(instance);
            if (plugin) {
                PluginSettingsItem item;
                item.filePath = filePath;
                item.name = plugin->pluginName();
                item.version = plugin->pluginVersion();
                item.description = plugin->pluginDescription();
                item.iconPng = plugin->iconPngData();
                item.enabled = false;
                m_pluginItems.append(item);
            } else {
                Logger::instance().log("ERROR", "SettingsTab", "refreshPluginList",
                                       QString("Cast failed for %1: Not implementing IChannelPlugin").arg(filePath));
            }
            loader.unload();
        } else {
            Logger::instance().log("ERROR", "SettingsTab", "refreshPluginList",
                                   QString("Failed to load DLL %1: %2").arg(filePath).arg(loader.errorString()));
        }
    }
}

void SettingsTab::onSaveButtonClicked() {
    saveSettings();
    emit settingsSaved();
    QMessageBox::information(this, "設定保存", "設定を保存しました。");
}

void SettingsTab::onTestConnectionClicked() {
    QString channel = m_twitchChannelEdit->text();
    QString token = m_twitchTokenEdit->text();
    
    if (channel.isEmpty()) {
        QMessageBox::warning(this, "エラー", "チャンネルIDを入力してください。");
        return;
    }
    
    m_twitchStatusLabel->setText("🟡 接続試行中...");
    
    // 一時的に再接続を実行
    QMetaObject::invokeMethod(m_controller->twitchCollector(), "disconnectFromTwitch", Qt::QueuedConnection);
    QMetaObject::invokeMethod(m_controller->twitchCollector(), "connectToTwitch", Qt::QueuedConnection, Q_ARG(QString, channel), Q_ARG(QString, token));
}

void SettingsTab::onCopyUrlClicked() {
    QString url = m_obsUrlCombo->currentText();
    QGuiApplication::clipboard()->setText(url);
    QMessageBox::information(this, "コピー完了", "ブラウザソース用URLをクリップボードにコピーしました。");
}

void SettingsTab::onMoveUpClicked() {
    int row = m_pluginListWidget->currentRow();
    if (row <= 0) return;
    
    QListWidgetItem* item = m_pluginListWidget->takeItem(row);
    m_pluginListWidget->insertItem(row - 1, item);
    m_pluginListWidget->setCurrentRow(row - 1);
    
    m_pluginItems.swapItemsAt(row, row - 1);
}

void SettingsTab::onMoveDownClicked() {
    int row = m_pluginListWidget->currentRow();
    if (row < 0 || row >= m_pluginListWidget->count() - 1) return;
    
    QListWidgetItem* item = m_pluginListWidget->takeItem(row);
    m_pluginListWidget->insertItem(row + 1, item);
    m_pluginListWidget->setCurrentRow(row + 1);
    
    m_pluginItems.swapItemsAt(row, row + 1);
}

void SettingsTab::onPluginSelectionChanged() {
    int row = m_pluginListWidget->currentRow();
    if (row < 0 || row >= m_pluginItems.size()) {
        m_pluginNameVal->setText("-");
        m_pluginVerVal->setText("-");
        m_pluginDescVal->setText("-");
        m_pluginPathVal->setText("-");
        return;
    }
    
    const PluginSettingsItem& item = m_pluginItems[row];
    m_pluginNameVal->setText(item.name);
    m_pluginVerVal->setText(item.version);
    m_pluginDescVal->setText(item.description);
    m_pluginPathVal->setText(item.filePath);
}

void SettingsTab::onTtsEngineToggled() {
    if (m_ttsBouyomiRadio->isChecked()) {
        m_ttsPortSpin->setValue(50001);
    } else {
        m_ttsPortSpin->setValue(50021);
    }
}

void SettingsTab::onTwitchTokenRetrieved(const QString& retrievedToken) {
    m_twitchTokenEdit->setText(retrievedToken);
    saveSettings();
}

void SettingsTab::onTwitchConnectionStatusChanged(bool connected, const QString& accountName) {
    if (connected) {
        m_twitchStatusLabel->setText(QString("🟢 接続完了 (%1)").arg(accountName));
    } else {
        m_twitchStatusLabel->setText("🔴 未接続");
    }
}
