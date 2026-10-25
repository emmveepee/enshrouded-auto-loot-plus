#pragma once
// Exported, read-only-to-observers status. No file I/O or process-wide scans.
struct LootRuntimeStatus {
 uint64_t revision=0,version=1,tick=0,world=0,player=0;
 uint64_t menuReady=0,enabled=0,automatic=0,corpses=0,radiusMm=0;
 uint64_t foreground=0,menuBlocked=0,flags=0,mask=0,digital=0;
 uint64_t directSource=0,coldSource=0,resourceSource=0,blockers=0;
 uint64_t interactionHost=0,sequence=0,interactionReady=0,disabled=0;
 uint64_t directedGate=0,directedAuto=0,directedActive=0,directReady=0;
 uint64_t rawMenu=0,ownedMenu=0,settings=0,nativeInstalled=0;
 uint64_t paused=0,targeted=0,corpseOpening=0,resourceOpening=0;
 uint64_t lastScanAge=0,capture=0;
 uint64_t begins=0,taps=0,accepted=0,timeouts=0,aborts=0,poseFails=0,movementFails=0,gainBatches=0,gainTypes=0;
 uint64_t intended=0,selected=0,protectedOffers=0,confirmations=0,elapsed=0,distanceMm=0,resourceCategory=0;
 uint64_t deferredRangeReceipts=0,lastDeferredHost=0,lastDeferredDistanceMm=0;
};
extern "C" __declspec(dllexport) LootRuntimeStatus ClientLootRuntimeStatus{};
