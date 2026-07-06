#include "TtsManager.h"
#include "BouyomiClient.h"
#include "VoiceVoxClient.h"
#include "logger/Logger.h"

TtsManager::TtsManager() 
    : m_workerThread(nullptr)
    , m_activeClient(nullptr)
    , m_activeEngineType(-1) 
{
}

TtsManager::~TtsManager() {
    stopManager();
    if (m_activeClient) {
        delete m_activeClient;
        m_activeClient = nullptr;
    }
}

void TtsManager::startManager() {
    if (!m_workerThread) {
        m_workerThread = new QThread();
        this->moveToThread(m_workerThread);
        m_workerThread->start();
        Logger::instance().log("INFO", "TtsManager", "startManager", "TTS Worker Thread started.");
    }
}

void TtsManager::stopManager() {
    if (m_activeClient) {
        m_activeClient->stop();
    }
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait();
        delete m_workerThread;
        m_workerThread = nullptr;
        Logger::instance().log("INFO", "TtsManager", "stopManager", "TTS Worker Thread stopped.");
    }
}

void TtsManager::requestSpeak(const QString& text, const QString& speakerId, int speed, int pitch, int volume) {
    QMutexLocker locker(&m_mutex);
    if (m_activeClient) {
        m_activeClient->speak(text, speakerId, speed, pitch, volume);
    } else {
        Logger::instance().log("WARNING", "TtsManager", "requestSpeak", "No active TTS client initialized");
    }
}

void TtsManager::updateSettings(int activeEngineType, const QJsonObject& settings) {
    QMutexLocker locker(&m_mutex);
    
    // エンジン種別または設定内容が変更された場合にクライアントを再構築・再初期化
    if (m_activeEngineType != activeEngineType || m_currentSettings != settings || !m_activeClient) {
        Logger::instance().log("INFO", "TtsManager", "updateSettings", 
                               QString("Updating settings. ActiveEngineType: %1 -> %2")
                               .arg(m_activeEngineType).arg(activeEngineType));
                               
        if (m_activeClient) {
            m_activeClient->stop();
            delete m_activeClient;
            m_activeClient = nullptr;
        }
        
        m_activeEngineType = activeEngineType;
        m_currentSettings = settings;
        
        if (m_activeEngineType == 0) {
            m_activeClient = new BouyomiClient();
        } else if (m_activeEngineType == 1) {
            m_activeClient = new VoiceVoxClient();
        }
        
        if (m_activeClient) {
            m_activeClient->initialize(settings);
        } else {
            Logger::instance().log("ERROR", "TtsManager", "updateSettings", "Failed to create TTS client instance");
        }
    }
}
