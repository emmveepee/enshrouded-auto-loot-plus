#include "corpse_transfer_policy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
using corpse_transfer::Candidate;
static void Check(bool v,const char* message){if(!v){std::printf("FAIL %s\n",message);std::exit(1);}}
int main(){
 // Actual incident: NightLily258843 accepted, then its same sequence/host was
 // repeated after removal while opening another plant one second later.
 corpse_transfer::VerifiedAck ack{0x1234,2,258843,2227211401u,10000};
 Check(corpse_transfer::ReplayedAck(ack,0x1234,2,258843,2227211401u,11000,true,false),"recent verified removed flower acknowledgement cannot disable all pickup");
 Check(!corpse_transfer::ReplayedAck(ack,0x1234,2,258844,2227211401u,11000,true,false),"new unverified target still safety stops");
 Check(!corpse_transfer::ReplayedAck(ack,0x1235,2,258843,2227211401u,11000,true,false),"world changes invalidate old acknowledgement");
 Check(!corpse_transfer::ReplayedAck(ack,0x1234,3,258843,2227211401u,11000,true,false),"player changes invalidate acknowledgement");
 Check(!corpse_transfer::ReplayedAck(ack,0x1234,2,258843,123,11000,true,false),"different action sequence is not a replay");
 Check(corpse_transfer::ReplayedAck(ack,0x1234,2,258843,358495932u,11000,true,false,true),"removed approved pickup may report its acknowledgement during later locomotion sequence");
 Check(!corpse_transfer::ReplayedAck(ack,0x1234,2,258844,358495932u,11000,true,false,true),"removed but never approved object still stops");
 Check(!corpse_transfer::ReplayedAck(ack,0x1234,2,258843,358495932u,40001,true,false,true),"removed object exception does not extend acknowledgement expiry");
 corpse_transfer::VerifiedAck history[]={ack,{0x1234,2,258844,2227211401u,10500}};bool foundOld=false;
 for(const auto& prior:history)foundOld|=corpse_transfer::ReplayedAck(prior,0x1234,2,258843,358495932u,11000,true,false,true);
 Check(foundOld,"out of order acknowledgement can match earlier approved pickup rather than only latest");
 Check(!corpse_transfer::ReplayedAck(ack,0x1234,2,258843,2227211401u,15001,true,false),"old acknowledgements expire");
 Check(!corpse_transfer::ReplayedAck(ack,0x1234,2,258843,2227211401u,11000,false,false),"present unknown or furniture target still safety stops");
 Check(!corpse_transfer::ReplayedAck(ack,0x1234,2,258843,2227211401u,11000,true,true),"quest protection never bypassed");
 Check(!corpse_transfer::ProtectedTapReady(true,false,123,0,123,2,120),"missing server target table never permits protected pickup");
 Check(!corpse_transfer::ProtectedTapReady(true,true,456,0,123,2,120),"selected growing crop or another object prevents tap");
 Check(!corpse_transfer::ProtectedTapReady(true,true,123,1,123,2,120),"nonentity target never permits protected pickup");
 Check(!corpse_transfer::ProtectedTapReady(true,true,123,0,123,1,120),"one stale target observation is insufficient");
 Check(!corpse_transfer::ProtectedTapReady(true,true,123,0,123,2,99),"selection priming wait retained around growing crops");
 Check(corpse_transfer::ProtectedTapReady(true,true,123,0,123,2,120),"confirmed exact intended loot can be picked near protected crops");
 Check(corpse_transfer::ProtectedTapReady(false,false,0,0,123,0,16),"unmixed field keeps fast next-tick path");
 Check(corpse_transfer::PreferConfirmedSource(123,true,456,0,true)==456,"mixed patch prefers verified selected wild plant over fairness target");
 Check(corpse_transfer::ProtectedTapReady(true,true,456,0,456,2,120),"selected wild plant remains tappable beside growing crop");
 Check(corpse_transfer::PreferConfirmedSource(123,true,456,0,false)==123,"selected growing or excluded target never enters pickup queue");
 Check(corpse_transfer::PreferConfirmedSource(123,true,456,1,true)==123,"nonentity selection cannot redirect pickup");
 Check(corpse_transfer::PreferConfirmedSource(123,true,0x100000001ull,0,true)==123,"oversized selected ID cannot truncate to another entity");
 Check(corpse_transfer::PreferConfirmedSource(123,false,456,0,true)==123,"unmixed resource queue keeps fairness");
 Check(!corpse_transfer::CanTap(123,123,20),"repeated getter on same simulation tick never taps before priming");
 Check(!corpse_transfer::CanTap(123,124,0),"no zero-time tap");
 Check(corpse_transfer::CanTap(123,124,16),"next submitted input tick can pick up without50ms wait");
 Check(corpse_transfer::CanTap(0xffffffffu,0,16),"simulation tick wrap still advances pickup");
 Candidate full{true,true,true,true,false,4,0,0};
 Check(corpse_transfer::Eligible(full,10000),"unopened initialized corpse is eligible");
 auto empty=full;empty.amount=0;
 auto bed=full;bed.corpse=false;
 auto disabled=full;disabled.allowed=false;
 auto quest=full;quest.quest=true;
 auto unready=full;unready.initialized=false;
 for(auto c:{empty,bed,disabled,quest,unready})Check(!corpse_transfer::Eligible(c,10000),"blocked inventory rejected");
 auto waiting=full;waiting.lastRequest=9800;
 Check(!corpse_transfer::Eligible(waiting,10000),"same corpse is not spammed before acknowledgement");
 Check(corpse_transfer::Eligible(waiting,11000),"bounded retry resumes after cooldown");
 // Regression: an empty nearest corpse and a bed must not starve two full corpses.
 Candidate scene[]={empty,bed,full,full};unsigned selected[2]={99,99};
 for(unsigned pass=0;pass<2;++pass)for(unsigned i=0;i<4;++i)if(corpse_transfer::Eligible(scene[i],10000)){selected[pass]=i;scene[i].amount=0;break;}
 Check(selected[0]==2&&selected[1]==3,"both full corpses selected across acknowledgements");
 Check(corpse_transfer::NeedsOpening(empty,true,0,10000),"hidden loot can be opened");
 // Real stalled pile406036: LootPickup_Material_Critter_Parts, Corpse category,
 // initialized empty inventory and offered Pick verb2425743549. It previously
 // fell between corpse Loot-only and resource-category-only queues forever.
 Check(!corpse_transfer::NeedsOpening(full,true,0,10000),"populated loot uses transfer only");
 Check(!corpse_transfer::NeedsOpening(bed,true,0,10000),"bed cannot enter opening queue");
 Check(corpse_transfer::NeedsOpening(empty,true,0,10000),"classified corpse eligibility has no verb input");
 for(auto c:{bed,disabled,quest}){c.amount=0;Check(!corpse_transfer::NeedsOpening(c,true,0,10000),"non-corpse, checkbox and quest guards block cold opening");}
 Check(!corpse_transfer::NeedsOpening(empty,false,0,10000),"unoffered corpse cannot be opened");
 Check(!corpse_transfer::NeedsOpening(empty,true,9500,10000),"empty pile has bounded retry cooldown");
 auto cold=empty;Candidate piles[]={cold,cold};uint64_t opened[]={0,0};
 for(unsigned pass=0;pass<2;++pass){unsigned chosen=99;for(unsigned i=0;i<2;++i)if(corpse_transfer::NeedsOpening(piles[i],true,opened[i],10000)){chosen=i;opened[i]=10000;break;}Check(chosen==pass,"cold piles progress without nearest starvation");}
 Check(corpse_transfer::AcceptedCorpse(true,true,false),"another nearby known corpse is valid progress");
 Check(!corpse_transfer::AcceptedCorpse(false,true,false),"furniture and unknown target stop opening");
 Check(!corpse_transfer::AcceptedCorpse(true,false,false),"distant target stops opening");
 // Captured136stop: host405693 known1/range0/quest0/position1. Receipt
 // rejection must not permanently latch otherwise eligible nearby plants off.
 Check(corpse_transfer::DeferMovedReceipt(true,false,false,true),"known moved receipt waits for idle without global safety stop");
 Check(!corpse_transfer::AcceptedCorpse(true,false,false),"deferred receipt is not successful pickup or new range permission");
 Check(!corpse_transfer::DeferMovedReceipt(false,false,false,true),"unknown/protected live target still rejected");
 Check(!corpse_transfer::DeferMovedReceipt(true,false,true,true),"quest receipt never deferred through moved-target exception");
 Check(!corpse_transfer::DeferMovedReceipt(true,false,false,false),"missing position uses exact issued/replayed safeguards");
 Check(!corpse_transfer::DeferMovedReceipt(true,true,false,true),"normal in-range receipt follows ordinary acceptance");
 Check(!corpse_transfer::AcceptedCorpse(true,true,true),"quest target stops opening");
 Check(corpse_transfer::PreferOpening(0,16,9000,1),"untried farther corpse outranks empty nearest retry");
 Check(!corpse_transfer::PreferOpening(9000,1,0,16),"empty nearest cannot starve an untried corpse");
 // Current scene: opened corpse plus two loose piles whose server selection
 // never confirms. Unsent attempts must age in the queue too, not stay new.
 uint64_t lastAttempt[]={100,0,0};double distances[]={0.45*0.45,4.3*4.3,4.44*4.44};
 const unsigned expectedOrder[]={1,2,0};
 for(unsigned pass=0;pass<3;++pass){unsigned selected=99;uint64_t best=UINT64_MAX;double bestD=100;
  for(unsigned i=0;i<3;++i)if(corpse_transfer::PreferOpening(lastAttempt[i],distances[i],best,bestD)){selected=i;best=lastAttempt[i];bestD=distances[i];}
  Check(selected==expectedOrder[pass],"unselectable loose piles yield and cannot permanently starve corpse retry");lastAttempt[selected]=200+pass;
 }
 Check(corpse_transfer::NeedsOpening(empty,true,9500,12500),"failed opening retry does not require ten seconds");
 Check(corpse_transfer::InteractionReady(0xff1faef,0xff1faef,0x23c4000,0,0x800),"idle actor ignores stale playback restriction");
 Check(!corpse_transfer::InteractionReady(0xff1faef,0xff1faef,0xe3c62ef,3481069144u,0x800),"ongoing corpse action must finish before next opening");
 Check(corpse_transfer::InteractionReady(0xff1faef,0xff1faef,0x7fdeaef,2085681474u,0x800),"ordinary locomotion permits interaction");
 Check(corpse_transfer::NeedsOpening(empty,true,0,10000,3,false),"retry count never permanently strands a valid pile");
 Check(!corpse_transfer::NeedsOpening(empty,true,9500,10000,3,false),"retry burst has a backoff");
 Check(corpse_transfer::NeedsOpening(empty,true,9500,14500,3,false),"known corpse retries resume after backoff instead of stopping permanently");
 Check(corpse_transfer::NeedsOpening(empty,true,9500,11000,3,false),"post-burst retry no longer waits five seconds");
 Check(!corpse_transfer::NeedsOpening(empty,true,9500,10999,3,false),"post-burst retry remains bounded at 1.5 seconds");
 Check(!corpse_transfer::NeedsOpening(empty,true,9500,10249,0,false),"opening burst cannot spam every frame");
 Check(corpse_transfer::NeedsOpening(empty,true,9500,10250,0,false),"opening burst retry begins at 750ms");
 auto retryFull=full;retryFull.attempts=3;retryFull.lastRequest=9500;
 Check(!corpse_transfer::Eligible(retryFull,10999),"populated transfer retry also backs off");
 Check(corpse_transfer::Eligible(retryFull,11000),"populated transfer cannot strand a valid inventory after three misses");
 Check(!corpse_transfer::NeedsOpening(empty,true,0,10000,0,true),"confirmed depleted corpse never repeatedly opens");
 Check(corpse_transfer::BlocksOpening(false,false,1.2*1.2,5),"nearby bed blocks contextual opening");
 Check(!corpse_transfer::BlocksOpening(false,false,7.5*7.5,5),"disabled container outside pickup search reach does not block all corpses");
 Check(corpse_transfer::BlocksOpening(false,true,4*4,5),"disabled chest within pickup reach still blocks");
 Check(corpse_transfer::BlocksOpening(false,false,2*2,1),"small slider cannot shrink minimum furniture guard");
 Check(corpse_transfer::ResourceEligible(true,true,true,false,0,10000,0),"enabled known harvest target is eligible");
 Check(!corpse_transfer::ResourceEligible(false,true,true,false,0,10000,0),"furniture cannot enter resource queue");
 Check(!corpse_transfer::ResourceEligible(true,false,true,false,0,10000,0),"resource checkbox respected");
 Check(!corpse_transfer::ResourceEligible(true,true,true,true,0,10000,0),"quest resource rejected");
 Check(!corpse_transfer::ResourceEligible(false,true,true,false,0,10000,0),"bed is not identified as resource loot");
 auto hidden=empty;hidden.inventory=false;hidden.initialized=false;
 Check(corpse_transfer::NeedsOpening(hidden,true,0,10000),"recognized offered loot need not expose inventory before opening");
 Check(!corpse_transfer::NeedsOpening(hidden,false,0,10000),"missing active offer still blocks hidden loot");
 corpse_transfer::VerifiedAck burstHistory[256]{};
 corpse_transfer::RememberAck(burstHistory,{0x1234,1,12253,2227211401u,1000});
 for(unsigned n=0;n<1000;++n)corpse_transfer::RememberAck(burstHistory,{0x1234,1,12276,2227211401u,1001+n});
 const corpse_transfer::VerifiedAck* delayed=nullptr;unsigned distinct=0;
 for(const auto& entry:burstHistory){if(entry.host)++distinct;if(entry.host==12253)delayed=&entry;}
 Check(distinct==2&&delayed,"duplicate callbacks preserve distinct older loot receipts");
 Check(delayed&&corpse_transfer::ReplayedAck(*delayed,0x1234,1,12253,2227211401u,7500,true,false,true),"observed six-second acknowledgement for vanished verified loot is stale");
 Check(delayed&&!corpse_transfer::ReplayedAck(*delayed,0x1234,1,12253,2227211401u,7500,false,false,false),"reused live unknown entity remains blocked");
 Check(delayed&&!corpse_transfer::ReplayedAck(*delayed,0x1234,1,12253,2227211401u,31001,true,false,true),"vanished receipt still has a finite thirty-second window");
 Check(delayed&&!corpse_transfer::ReplayedAck(*delayed,0x9999,1,12253,2227211401u,7500,true,false,true),"another world cannot reuse a receipt");
 Check(delayed&&!corpse_transfer::ReplayedAck(*delayed,0x1234,2,12253,2227211401u,7500,true,false,true),"another player cannot reuse a receipt");
 Check(delayed&&!corpse_transfer::ReplayedAck(*delayed,0x1234,1,12253,2227211401u,7500,true,true,true),"quest guard remains active");
 corpse_transfer::IssuedLoot issued{0x1234,1,2414610597u,1000};
 Check(corpse_transfer::GoneIssued(issued,0x1234,1,2414610597u,4000,true),"first callback for just-issued now-absent loot can be deferred");
 Check(!corpse_transfer::GoneIssued(issued,0x1234,1,2414610597u,4000,false),"live unknown target still stops");
 Check(!corpse_transfer::GoneIssued(issued,0x1234,1,2414610598u,4000,true),"unissued vanished target still stops");
 Check(!corpse_transfer::GoneIssued(issued,0x9999,1,2414610597u,4000,true),"issued target cannot cross worlds");
 Check(!corpse_transfer::GoneIssued(issued,0x1234,2,2414610597u,4000,true),"issued target cannot cross players");
 Check(!corpse_transfer::GoneIssued(issued,0x1234,1,2414610597u,6001,true),"issued target has bounded five-second grace");
 Check(!corpse_transfer::GoneIssued(issued,0x1234,1,2414610597u,999,true),"future issued receipt fails closed");
 std::puts("PASS corpse-transfer selection regression checks; native policy test only");
}



