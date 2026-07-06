#pragma once
#include <QObject>
#include "shared/plugin_interface.h"

class SignalDispatcher : public QObject {
    Q_OBJECT
public:
    static SignalDispatcher& instance();

signals:
    // 下り方向のシグナル
    void commentReceived(const TwitchComment& comment);
    void rewardRedeemed(const TwitchRewardRedemption& redemption);
    void tick();

public slots:
    // コアから受け取り、各プラグインへディスパッチする
    void dispatchComment(const TwitchComment& comment);
    void dispatchReward(const TwitchRewardRedemption& redemption);
    void dispatchTick();

private:
    SignalDispatcher() = default;
    ~SignalDispatcher() = default;
    SignalDispatcher(const SignalDispatcher&) = delete;
    SignalDispatcher& operator=(const SignalDispatcher&) = delete;
};
