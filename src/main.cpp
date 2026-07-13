#include <QApplication>
#include "core/AppController.h"
#include "ui/MainWindow.h"
#include "logger/Logger.h"
#include "TrustChainCore.hpp"
#include "TrustChainQt.hpp"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);

    // 1. TrustChain 署名・認証トークンの検証
    TrustChain::Core trustChainCore;
    TrustChain::AuthStatus authStatus = trustChainCore.verifyToken();
    
    if (authStatus == TrustChain::AuthStatus::Terminated) {
        // トークン無効化時は強制終了
        TrustChain::Core::terminateApplication("TwitchChannelManagementTool: Invalid or revoked license token.");
        return -1;
    }
    
    // 2. コアコントローラの初期化
    AppController controller;
    controller.initialize();
    
    // 3. メインウィンドウの作成
    MainWindow window(&controller);
    
    // 4. TrustChain ウォーターマークの適用 (非公式ビルドや認証失敗時の表示制御)
    TrustChain::QtHelper::applyWatermark(&window, authStatus);
    
    // 5. ウィンドウ表示とイベントループ開始
    window.show();
    
    int execResult = app.exec();
    
    return execResult;
}
