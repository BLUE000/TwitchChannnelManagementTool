#include "Logger.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QTextStream>
#include "cipher_engine.h"

Logger& Logger::instance() {
    static Logger inst;
    return inst;
}

Logger::Logger() : m_enableLogging(false), m_cipherKey("TCMT_DefaultSecKey2026") {
    loadDebugConfig();
}

void Logger::loadDebugConfig() {
    m_enableLogging = false;
    QString appDir = QCoreApplication::applicationDirPath();
    QString configPath = QDir(appDir).filePath("debug_config.json");
    
    QFile file(configPath);
    if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QByteArray data = file.readAll();
        file.close();
        
        QJsonDocument doc = QJsonDocument::fromJson(data);
        if (!doc.isNull() && doc.isObject()) {
            QJsonObject obj = doc.object();
            if (obj.contains("enable_logging")) {
                m_enableLogging = obj.value("enable_logging").toBool();
            }
        }
    }
}

void Logger::log(const QString& action, const QString& className, const QString& funcName, const QString& description) {
    if (!m_enableLogging) {
        return;
    }
    
    QMutexLocker locker(&m_mutex);
    
    QDateTime current = QDateTime::currentDateTime();
    QString dateMonth = current.toString("yyyy-MM");
    QString timeDetail = current.toString("dd-hh-mm-ss");
    
    // 平文ログエントリを生成
    QString plainLine = QString("[%1][%2][%3][%4][%5][%6]")
                          .arg(dateMonth)
                          .arg(timeDetail)
                          .arg(action)
                          .arg(className)
                          .arg(funcName)
                          .arg(description);
                          
    // TransCipherで暗号化
    CipherResult result = CipherEngine::encrypt(plainLine.toUtf8(), m_cipherKey, AesMode::Mandatory);
    if (!result.isSuccess()) {
        return;
    }
    
    QByteArray base64Line = result.data().toBase64() + "\n";
    
    QString appDir = QCoreApplication::applicationDirPath();
    QDir(appDir).mkpath("logs");
    QString logFilePath = QDir(appDir).filePath(QString("logs/%1.log").arg(dateMonth));
    
    QFile logFile(logFilePath);
    if (logFile.open(QIODevice::WriteOnly | QIODevice::Append)) {
        logFile.write(base64Line);
        logFile.close();
    }
}
