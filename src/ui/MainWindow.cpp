#include "MainWindow.h"
#include "ui_MainWindow.h"
#include <QHBoxLayout>
#include <QScreen>
#include <QCloseEvent>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFileInfo>
#include "logger/Logger.h"
#include "cipher_engine.h"

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

MainWindow::MainWindow(AppController* controller, QWidget* parent)
    : QMainWindow(parent)
    , ui(new Ui::MainWindow)
    , m_controller(controller)
    , m_sidebar(nullptr)
    , m_stackedWidget(nullptr)
    , m_settingsTab(nullptr)
{
    ui->setupUi(this);
    setupUiManual();
    applyTheme();

    // Twitch接続ステータスの変更シグナルとバインド
    connect(m_controller->twitchCollector(), &TwitchEventCollector::connectionStatusChanged,
            this, &MainWindow::updateStatusBarTwitch);

    // 設定保存時のシグナルをバインドして、プラグイン表示タブを再同期
    connect(m_settingsTab, &SettingsTab::settingsSaved, this, &MainWindow::onSettingsSaved);

    // 初期タブ同期
    syncPluginTabs();

    // 保存位置の復元
    restoreWindowState();
}

MainWindow::~MainWindow() {
    delete ui;
}

void MainWindow::setupUiManual() {
    // 画面中央レイアウト
    QWidget* centralWidget = new QWidget(this);
    QHBoxLayout* layout = new QHBoxLayout(centralWidget);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 左サイドバー (QListWidget)
    m_sidebar = new QListWidget(centralWidget);
    m_sidebar->setObjectName("sidebar");
    m_sidebar->setFixedWidth(80);
    m_sidebar->setIconSize(QSize(48, 48));
    m_sidebar->setFrameShape(QFrame::NoFrame);
    connect(m_sidebar, &QListWidget::currentRowChanged, this, &MainWindow::onSidebarSelectionChanged);

    // 右メインコンテンツ領域 (QStackedWidget)
    m_stackedWidget = new QStackedWidget(centralWidget);

    layout->addWidget(m_sidebar);
    layout->addWidget(m_stackedWidget);
    
    setCentralWidget(centralWidget);

    // 設定画面タブの生成 (常にインスタンスを維持)
    m_settingsTab = new SettingsTab(m_controller, m_stackedWidget);

    // 最下部ステータスバー表示用
    m_statusTwitch = new QLabel("🔴 Twitch: 未接続", this);
    m_statusObs = new QLabel("🔴 OBS Server: 停止", this);
    m_statusTts = new QLabel("🟢 TTS: 待機中", this);

    statusBar()->addPermanentWidget(m_statusTwitch, 1);
    statusBar()->addPermanentWidget(m_statusObs, 1);
    statusBar()->addPermanentWidget(m_statusTts, 1);
}

void MainWindow::applyTheme() {
    QString qss = R"(
        /* 全体背景と標準フォント色 */
        QMainWindow, QScrollArea, QScrollArea > QWidget {
            background-color: #0F0F12;
            color: #E2E8F0;
        }
        
        /* ラベルの文字色 */
        QLabel {
            color: #E2E8F0;
        }
        
        /* ラジオボタンのスタイル */
        QRadioButton {
            color: #E2E8F0;
            spacing: 6px;
        }
        
        /* グループボックスのスタイル */
        QGroupBox {
            border: 1px solid #2D2D3A;
            border-radius: 8px;
            margin-top: 16px;
            padding-top: 16px;
            font-weight: bold;
            color: #9146FF;
            background-color: #16161F;
        }
        QGroupBox::title {
            subcontrol-origin: margin;
            subcontrol-position: top left;
            left: 12px;
            padding: 0 4px;
        }
        
        /* 左サイドバー */
        QListWidget#sidebar {
            background-color: #16161F;
            border: none;
            border-right: 1px solid #2D2D3A;
            padding-top: 10px;
        }
        QListWidget#sidebar::item {
            padding: 6px;
            border-radius: 8px;
            margin: 6px 8px;
        }
        QListWidget#sidebar::item:hover {
            background-color: #2D2D3D;
        }
        QListWidget#sidebar::item:selected {
            background-color: #9146FF;
        }
        
        /* 入力エリア */
        QLineEdit, QSpinBox, QComboBox {
            background-color: #22222E;
            border: 1px solid #2D2D3A;
            border-radius: 6px;
            padding: 6px 10px;
            color: #F7FAFC;
        }
        QLineEdit:focus, QSpinBox:focus, QComboBox:focus {
            border: 1px solid #9146FF;
        }
        
        /* プッシュボタン */
        QPushButton {
            background-color: #2D2D3E;
            border: 1px solid #3F3F56;
            border-radius: 6px;
            padding: 8px 16px;
            color: #F7FAFC;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: #3F3F56;
            border: 1px solid #9146FF;
        }
        QPushButton:pressed {
            background-color: #1A1A28;
        }
        
        /* ステータスバー */
        QStatusBar {
            background-color: #12121A;
            border-top: 1px solid #2D2D3A;
            color: #A0AEC0;
        }
        QStatusBar QLabel {
            color: #A0AEC0;
            background: transparent;
            border: none;
        }
    )";
    this->setStyleSheet(qss);
}

void MainWindow::syncPluginTabs() {
    // 現在の選択位置を記憶
    int currentIdx = m_sidebar->currentRow();
    QString currentKey = "";
    if (currentIdx >= 0 && currentIdx < m_tabs.size()) {
        currentKey = m_tabs[currentIdx].filePath;
    }

    // 設定ファイル settings.bin をデコード
    QString appDir = QCoreApplication::applicationDirPath();
    QString settingsPath = QDir(appDir).filePath("settings.bin");
    
    QJsonObject settingsObj;
    QFile file(settingsPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray encryptedData = file.readAll();
        file.close();
        
        // DPAPI または TransCipher を用いた復号
        QByteArray rawJson;
#ifdef Q_OS_WIN
        if (encryptedData.startsWith("TCF")) {
            CipherResult res = CipherEngine::decrypt(encryptedData, "TCMT_Settings_BackupKey");
            if (res.isSuccess()) rawJson = res.data();
        } else {
            DATA_BLOB input;
            input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encryptedData.constData()));
            input.cbData = encryptedData.size();
            DATA_BLOB output;
            if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output)) {
                rawJson = QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData);
                LocalFree(output.pbData);
            }
        }
#else
        CipherResult res = CipherEngine::decrypt(encryptedData, "TCMT_Settings_BackupKey");
        if (res.isSuccess()) rawJson = res.data();
#endif
        QJsonDocument doc = QJsonDocument::fromJson(rawJson);
        if (!doc.isNull() && doc.isObject()) {
            settingsObj = doc.object();
        }
    }

    QJsonArray pluginOrder = settingsObj.value("plugin_order").toArray();
    QJsonArray enabledPlugins = settingsObj.value("enabled_plugins").toArray();
    
    QSet<QString> enabledSet;
    for (int i = 0; i < enabledPlugins.size(); ++i) {
        enabledSet.insert(enabledPlugins[i].toString());
    }

    // 現在ロード済みのプラグイン一覧を取得して、無効化されたものをアンロード
    QList<LoadedPlugin> loaded = m_controller->pluginLoader()->loadedPlugins();
    for (const LoadedPlugin& lp : loaded) {
        QFileInfo fi(lp.filePath);
        if (!enabledSet.contains(fi.fileName())) {
            m_controller->pluginLoader()->unloadPlugin(lp.filePath);
        }
    }

    // 有効化されたプラグインを順序通りにロード（既にロード済みならスキップ）
    QList<TabMapping> newTabs;
    
    // スキャンしてDLL絶対パスをマッピング
    QList<QString> allScanned = m_controller->pluginLoader()->scanPlugins();
    QMap<QString, QString> filenameToFullPath;
    for (const QString& fp : allScanned) {
        QFileInfo fi(fp);
        filenameToFullPath[fi.fileName()] = fp;
    }

    for (int i = 0; i < pluginOrder.size(); ++i) {
        QString filename = pluginOrder[i].toString();
        if (enabledSet.contains(filename) && filenameToFullPath.contains(filename)) {
            QString fullPath = filenameToFullPath[filename];
            IChannelPlugin* plugin = m_controller->pluginLoader()->loadPlugin(fullPath, m_controller, m_stackedWidget);
            if (plugin) {
                // ロード済みの LoadedPlugin 情報を取得
                LoadedPlugin lp;
                for (const LoadedPlugin& l : m_controller->pluginLoader()->loadedPlugins()) {
                    if (l.filePath == fullPath) {
                        lp = l;
                        break;
                    }
                }
                
                TabMapping tm;
                tm.filePath = fullPath;
                tm.widget = lp.widget;
                newTabs.append(tm);
            }
        }
    }

    // UIの切り替え
    m_sidebar->clear();
    
    // 古いウィジェットを QStackedWidget からリムーブ (delete はしない)
    for (int i = m_stackedWidget->count() - 1; i >= 0; --i) {
        QWidget* w = m_stackedWidget->widget(i);
        m_stackedWidget->removeWidget(w);
    }

    m_tabs.clear();

    // 有効なプラグインを追加
    for (const TabMapping& tm : newTabs) {
        m_stackedWidget->addWidget(tm.widget);
        
        QListWidgetItem* sidebarItem = new QListWidgetItem(m_sidebar);
        
        // プラグイン名とアイコンの取得
        QString name = "";
        QByteArray iconPng;
        for (const LoadedPlugin& lp : m_controller->pluginLoader()->loadedPlugins()) {
            if (lp.filePath == tm.filePath) {
                name = lp.instance->pluginName();
                iconPng = lp.instance->iconPngData();
                break;
            }
        }
        
        sidebarItem->setText("");
        sidebarItem->setToolTip(name);
        if (!iconPng.isEmpty()) {
            QPixmap pix;
            if (pix.loadFromData(iconPng, "PNG")) {
                sidebarItem->setIcon(QIcon(pix));
            }
        }
        
        m_tabs.append(tm);
    }

    // 設定画面タブ（最下部固定）の追加
    m_stackedWidget->addWidget(m_settingsTab);
    
    QListWidgetItem* settingsItem = new QListWidgetItem(m_sidebar);
    settingsItem->setText("");
    settingsItem->setToolTip("設定");
    
    QString iconPath = QDir(appDir).filePath("pic/Config.png");
    if (QFile::exists(iconPath)) {
        settingsItem->setIcon(QIcon(iconPath));
    }
    
    TabMapping tmSettings;
    tmSettings.filePath = ""; // 設定タブは空欄
    tmSettings.widget = m_settingsTab;
    m_tabs.append(tmSettings);

    // 選択位置の復元
    int selectIdx = -1;
    for (int i = 0; i < m_tabs.size(); ++i) {
        if (m_tabs[i].filePath == currentKey) {
            selectIdx = i;
            break;
        }
    }
    
    if (selectIdx != -1) {
        m_sidebar->setCurrentRow(selectIdx);
    } else {
        // デフォルトは最初のタブ、または設定画面
        m_sidebar->setCurrentRow(0);
    }
}

void MainWindow::onSidebarSelectionChanged() {
    int row = m_sidebar->currentRow();
    if (row >= 0 && row < m_tabs.size()) {
        m_stackedWidget->setCurrentWidget(m_tabs[row].widget);
    }
}

void MainWindow::onSettingsSaved() {
    // 設定変更によるOBSサーバーポートの再読み込み
    QString appDir = QCoreApplication::applicationDirPath();
    QString settingsPath = QDir(appDir).filePath("settings.bin");
    
    int obsPort = 8081;
    QFile file(settingsPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray encryptedData = file.readAll();
        file.close();
        
        QByteArray rawJson;
#ifdef Q_OS_WIN
        if (encryptedData.startsWith("TCF")) {
            CipherResult res = CipherEngine::decrypt(encryptedData, "TCMT_Settings_BackupKey");
            if (res.isSuccess()) rawJson = res.data();
        } else {
            DATA_BLOB input;
            input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encryptedData.constData()));
            input.cbData = encryptedData.size();
            DATA_BLOB output;
            if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output)) {
                rawJson = QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData);
                LocalFree(output.pbData);
            }
        }
#else
        CipherResult res = CipherEngine::decrypt(encryptedData, "TCMT_Settings_BackupKey");
        if (res.isSuccess()) rawJson = res.data();
#endif
        QJsonDocument doc = QJsonDocument::fromJson(rawJson);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            obsPort = obj.value("obs_port").toInt(8081);
            
            // TTS設定の更新
            int ttsEngine = obj.value("tts_engine").toInt(0);
            QJsonObject ttsSettings;
            ttsSettings["host"] = obj.value("tts_host").toString("localhost");
            ttsSettings["port"] = obj.value("tts_port").toInt(50001);
            
            m_controller->ttsManager()->updateSettings(ttsEngine, ttsSettings);
        }
    }

    // OBSサーバーを再起動
    m_controller->stopAllServices();
    m_controller->startAllServices(obsPort);
    
    m_statusObs->setText(QString("🟢 OBS Server: ポート %1").arg(obsPort));

    // プラグイン画面タブを再同期
    syncPluginTabs();
}

void MainWindow::updateStatusBarTwitch(bool connected, const QString& accountName) {
    if (connected) {
        m_statusTwitch->setText(QString("🟢 Twitch: %1").arg(accountName));
    } else {
        m_statusTwitch->setText("🔴 Twitch: 未接続");
    }
}

void MainWindow::closeEvent(QCloseEvent* event) {
    saveWindowState();
    
    // スレッド・モジュールの停止
    m_controller->stopAllServices();
    
    event->accept();
}

void MainWindow::saveWindowState() {
    QByteArray geom = saveGeometry();
    QByteArray state = saveState();

    QString appDir = QCoreApplication::applicationDirPath();
    QString settingsPath = QDir(appDir).filePath("settings.bin");

    QJsonObject settingsObj;
    QFile file(settingsPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray encryptedData = file.readAll();
        file.close();
        QByteArray rawJson;
#ifdef Q_OS_WIN
        if (encryptedData.startsWith("TCF")) {
            CipherResult res = CipherEngine::decrypt(encryptedData, "TCMT_Settings_BackupKey");
            if (res.isSuccess()) rawJson = res.data();
        } else {
            DATA_BLOB input;
            input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encryptedData.constData()));
            input.cbData = encryptedData.size();
            DATA_BLOB output;
            if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output)) {
                rawJson = QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData);
                LocalFree(output.pbData);
            }
        }
#else
        CipherResult res = CipherEngine::decrypt(encryptedData, "TCMT_Settings_BackupKey");
        if (res.isSuccess()) rawJson = res.data();
#endif
        QJsonDocument doc = QJsonDocument::fromJson(rawJson);
        if (!doc.isNull() && doc.isObject()) {
            settingsObj = doc.object();
        }
    }

    settingsObj["window_geometry"] = QString::fromLatin1(geom.toBase64());
    settingsObj["window_state"] = QString::fromLatin1(state.toBase64());

    QJsonDocument doc(settingsObj);
    QByteArray rawJson = doc.toJson(QJsonDocument::Compact);
    QByteArray encrypted = rawJson;
#ifdef Q_OS_WIN
    DATA_BLOB input;
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(rawJson.constData()));
    input.cbData = rawJson.size();
    DATA_BLOB output;
    if (CryptProtectData(&input, L"TCMTSettings", NULL, NULL, NULL, 0, &output)) {
        encrypted = QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData);
        LocalFree(output.pbData);
    }
#else
    CipherResult res = CipherEngine::encrypt(rawJson, "TCMT_Settings_BackupKey");
    if (res.isSuccess()) encrypted = res.data();
#endif

    if (file.open(QIODevice::WriteOnly)) {
        file.write(encrypted);
        file.close();
    }
}

void MainWindow::restoreWindowState() {
    QString appDir = QCoreApplication::applicationDirPath();
    QString settingsPath = QDir(appDir).filePath("settings.bin");

    QJsonObject settingsObj;
    QFile file(settingsPath);
    if (file.open(QIODevice::ReadOnly)) {
        QByteArray encryptedData = file.readAll();
        file.close();
        QByteArray rawJson;
#ifdef Q_OS_WIN
        if (encryptedData.startsWith("TCF")) {
            CipherResult res = CipherEngine::decrypt(encryptedData, "TCMT_Settings_BackupKey");
            if (res.isSuccess()) rawJson = res.data();
        } else {
            DATA_BLOB input;
            input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(encryptedData.constData()));
            input.cbData = encryptedData.size();
            DATA_BLOB output;
            if (CryptUnprotectData(&input, NULL, NULL, NULL, NULL, 0, &output)) {
                rawJson = QByteArray(reinterpret_cast<char*>(output.pbData), output.cbData);
                LocalFree(output.pbData);
            }
        }
#else
        CipherResult res = CipherEngine::decrypt(encryptedData, "TCMT_Settings_BackupKey");
        if (res.isSuccess()) rawJson = res.data();
#endif
        QJsonDocument doc = QJsonDocument::fromJson(rawJson);
        if (!doc.isNull() && doc.isObject()) {
            settingsObj = doc.object();
        }
    }

    if (settingsObj.contains("window_geometry")) {
        QByteArray geom = QByteArray::fromBase64(settingsObj.value("window_geometry").toString().toLatin1());
        restoreGeometry(geom);
    }
    if (settingsObj.contains("window_state")) {
        QByteArray state = QByteArray::fromBase64(settingsObj.value("window_state").toString().toLatin1());
        restoreState(state);
    }

    // フェールセーフ：画面外チェック
    QScreen* primary = QGuiApplication::primaryScreen();
    if (primary) {
        QRect screenGeom = primary->geometry();
        if (!screenGeom.intersects(geometry())) {
            // 画面外に完全に飛び出している場合は中央に再配置
            setGeometry(QStyle::alignedRect(Qt::LeftToRight, Qt::AlignCenter, size(), screenGeom));
        }
    }

    // OBSサーバー初期稼働
    int obsPort = settingsObj.value("obs_port").toInt(8081);
    m_controller->startAllServices(obsPort);
    m_statusObs->setText(QString("🟢 OBS Server: ポート %1").arg(obsPort));

    // TTS初期設定適用
    int ttsEngine = settingsObj.value("tts_engine").toInt(0);
    QJsonObject ttsSettings;
    ttsSettings["host"] = settingsObj.value("tts_host").toString("localhost");
    ttsSettings["port"] = settingsObj.value("tts_port").toInt(50001);
    m_controller->ttsManager()->updateSettings(ttsEngine, ttsSettings);
}
