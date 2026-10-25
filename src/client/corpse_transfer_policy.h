#pragma once
#include <cstdint>
#include <cstddef>
namespace corpse_transfer {
// Prime selection for one submitted input tick, then tap. Never send both on
// the initial tick: the server must first see the selected camera direction.
inline constexpr uint64_t ScanMs=16,ReadyStableMs=1,AimSettleMs=1;
inline constexpr uint64_t StartSpacingMs=32,OpeningTimeoutMs=250;
inline bool CanTap(uint32_t startTick,uint32_t currentTick,uint64_t elapsed){return currentTick!=startTick&&elapsed>=AimSettleMs;}
inline bool ProtectedTapReady(bool protectedCrops,bool tablePresent,uint64_t selected,uint8_t type,uint32_t intended,uint32_t confirmations,uint64_t elapsed){return !protectedCrops||(tablePresent&&type==0&&selected==intended&&confirmations>=2&&elapsed>=100);}
// In a mixed patch use a validated native-selected resource, rather than
// fairness selecting a different eligible plant that can never confirm.
// Caller checks the selected resource's category, active offer, quest, range
// and retry state. An excluded crop must never redirect an interaction.
inline uint32_t PreferConfirmedSource(uint32_t candidate,bool protectedOffers,uint64_t selected,uint8_t type,bool eligible){return protectedOffers&&selected&&selected<=UINT32_MAX&&type==0&&eligible?uint32_t(selected):candidate;}
// A failed request must not park a nearby pile for five seconds. Keep a short
// three-attempt burst, then at most one contextual retry per 1.5 seconds.
inline bool RetryReady(uint64_t last,uint64_t now,uint32_t tries){return !last||now-last>=(tries<3?750u:1500u);}
inline bool ResourceVerb(uint32_t verb){return verb==48836331u||verb==1577711826u||verb==2425743549u;}
inline bool ResourceEligible(bool resource,bool allowed,bool offered,bool quest,uint64_t last,uint64_t now,uint32_t tries){return resource&&allowed&&offered&&!quest&&RetryReady(last,now,tries);}
struct Candidate {
 bool corpse=false,allowed=false,inventory=false,initialized=false,quest=false;
 uint32_t amount=0,attempts=0;
 uint64_t lastRequest=0;
};
inline bool Eligible(const Candidate& c,uint64_t now){
 return c.corpse&&c.allowed&&c.inventory&&c.initialized&&!c.quest&&c.amount>0&&
  (!c.lastRequest||now-c.lastRequest>=(c.attempts<3?300u:1500u));
}
// Opening hidden loot is distinct from transferring populated inventory. Give
// empty corpses their own cooldown so one empty pile cannot starve its neighbors.
// Classification establishes that this is loot. An active offer establishes
// that the game permits interaction. Its label is not an eligibility filter.
// Hidden inventory need not have replicated before requesting that interaction.
inline bool NeedsOpening(const Candidate& c,bool offered,uint64_t lastOpen,uint64_t now,uint32_t openings=0,bool depleted=false){
 return c.corpse&&c.allowed&&!c.quest&&!c.amount&&offered&&!depleted&&
  RetryReady(lastOpen,now,openings);
}
inline bool AcceptedCorpse(bool knownCorpse,bool inRange,bool quest){return knownCorpse&&inRange&&!quest;}
// A receipt describes an interaction the server already processed. The player
// may have moved outside the local slider radius before this callback arrived.
// Discard that known, enabled, non-quest receipt; do not count success, extend
// outgoing reach or permanently disable all future pickup. Unknown/protected
// or missing-position targets retain the separate strict receipt safeguards.
inline bool DeferMovedReceipt(bool known,bool inRange,bool quest,bool hasPosition){return known&&!inRange&&!quest&&hasPosition;}
struct VerifiedAck {uintptr_t world=0;uint32_t player=0,host=0,sequence=0;uint64_t at=0;};
struct IssuedLoot {uintptr_t world=0;uint32_t player=0,source=0;uint64_t at=0;bool animalProduce=false;};
// Collecting produce switches the offer back to Pet before the receipt arrives.
// Recognize only an exact issued collection receipt, never a new input permit.
inline bool IssuedProduceReceipt(const IssuedLoot& issued,uintptr_t world,uint32_t player,uint32_t host,uint64_t now,bool livingTameKnown){return livingTameKnown&&issued.animalProduce&&issued.source&&issued.world==world&&issued.player==player&&issued.source==host&&issued.at&&now>=issued.at&&now-issued.at<=5000;}
// A validated loot object may disappear between our tap and its first callback.
// This only defers an exact recently issued, now-absent target. It does not
// validate a new live target or treat the request as a successful pickup.
inline bool GoneIssued(const IssuedLoot& issued,uintptr_t world,uint32_t player,uint32_t host,uint64_t now,bool gone){return gone&&issued.source&&issued.world==world&&issued.player==player&&issued.source==host&&issued.at&&now>=issued.at&&now-issued.at<=5000;}
inline bool ReplayedAck(const VerifiedAck& ack,uintptr_t world,uint32_t player,uint32_t host,uint32_t sequence,uint64_t now,bool knownOrGone,bool quest,bool gone=false){return ack.at&&ack.host&&ack.world==world&&ack.player==player&&ack.host==host&&(ack.sequence==sequence||gone)&&now>=ack.at&&now-ack.at<=(gone?30000u:5000u)&&knownOrGone&&!quest;}
// Duplicate acknowledgements refresh one entry, rather than evicting distinct
// verified loot IDs during a burst. Only accepted, validated targets enter here.
template<size_t N> inline size_t RememberAck(VerifiedAck (&history)[N],const VerifiedAck& ack){
 size_t slot=0;
 for(size_t i=0;i<N;++i){if(history[i].world==ack.world&&history[i].player==ack.player&&history[i].host==ack.host){history[i]=ack;return i;}if(history[i].at<history[slot].at)slot=i;}
 history[slot]=ack;return slot;
}
inline bool PreferOpening(uint64_t last,double distance2,uint64_t selectedLast,double selectedDistance2){return last<selectedLast||(last==selectedLast&&distance2<=selectedDistance2);}
inline bool InteractionReady(uint32_t client,uint32_t network,uint32_t playback,uint32_t sequence,uint32_t mask){return (client&mask)&&(network&mask)&&(!sequence||(playback&mask));}
inline bool BlocksOpening(bool allowed,bool supported,double distance2,double radius){const double reach=radius*radius>9?radius*radius:9;return (!allowed||!supported)&&distance2<=reach;}
}

