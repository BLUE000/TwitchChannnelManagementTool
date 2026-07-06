#include "SignalDispatcher.h"

SignalDispatcher& SignalDispatcher::instance() {
    static SignalDispatcher inst;
    return inst;
}

void SignalDispatcher::dispatchComment(const TwitchComment& comment) {
    emit commentReceived(comment);
}

void SignalDispatcher::dispatchReward(const TwitchRewardRedemption& redemption) {
    emit rewardRedeemed(redemption);
}

void SignalDispatcher::dispatchTick() {
    emit tick();
}
