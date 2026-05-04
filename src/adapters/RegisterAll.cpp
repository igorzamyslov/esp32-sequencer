#include "RegisterAll.h"

namespace seqb {

void registerWaitBlock();
void registerRepeatBlock();
void registerIfBlock();
void registerWaitForTriggerBlock();
void registerWolBlock();
void registerWifiHopBlock();
void registerSamsungTizenKeysBlock();
void registerOnWifiPredicate();
void registerHostReachablePredicate();
void registerHttpRouteTrigger();
void registerBleMacTrigger();

void registerAllAdapters() {
    registerWaitBlock();
    registerRepeatBlock();
    registerIfBlock();
    registerWaitForTriggerBlock();
    registerWolBlock();
    registerWifiHopBlock();
    registerSamsungTizenKeysBlock();
    registerOnWifiPredicate();
    registerHostReachablePredicate();
    registerHttpRouteTrigger();
    registerBleMacTrigger();
}

}  // namespace seqb
