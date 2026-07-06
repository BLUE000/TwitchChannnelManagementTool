#pragma once
#include <QString>
#include <QByteArray>
#include <QMutex>

class Logger {
public:
    static Logger& instance();
    
    // ログ記録メインメソッド
    void log(const QString& action, const QString& className, const QString& funcName, const QString& description);
    
    // デバッグ設定の動的再読み込み
    void loadDebugConfig();

private:
    Logger();
    ~Logger() = default;
    
    // シングルトンとしてのコピー禁止
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    
    bool m_enableLogging;   // ログ出力の有効フラグ
    QString m_cipherKey;    // 難読化キー
    QMutex m_mutex;         // スレッドセーフ用のミューテックス
};
