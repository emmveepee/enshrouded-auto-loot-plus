// Client-only pickup/transfer investigation. MIT; no server changes.
// Runtime code addresses are found by checked signatures, never build RVAs.
#include <windows.h>
#include <tlhelp32.h>
#include <cstdint>
#include <cstdio>
#include <cstdarg>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <map>
#include "game_abi.h"
#include "radius_config.h"
#include "menu_config.h"
#include "component_catalog.h"
#include "template_policy.h"
#include "corpse_transfer_policy.h"
#include "loot_aim.h"
#include "runtime_status.h"

static std::wstring dir, ini, logPath;
static SRWLOCK logLock=SRWLOCK_INIT;
// Normal builds retain setup/errors, not synchronous per-item/per-corpse logs.
static bool diagnosticVerbose=false;
static uint8_t* textStart; static size_t textSize;
static bool disabled=false;
static std::vector<uint32_t> questItems;
static bool ShouldLog(const char* fmt){return diagnosticVerbose||strstr(fmt,"READY")||strstr(fmt,"INSTALLED")||strstr(fmt,"UNSUPPORTED")||strstr(fmt,"UNAVAILABLE")||strstr(fmt,"DISABLED")||strstr(fmt,"SAFETY_STOP")||strstr(fmt,"exception")||strstr(fmt,"EXCEPTION")||!strncmp(fmt,"LOOT_PROFILE",12);}
static void Log(const char* fmt,...) {
 if(!ShouldLog(fmt))return;
 AcquireSRWLockExclusive(&logLock); FILE* f=nullptr;
 static uint64_t logWindow=0;static unsigned logCount=0;const auto logNow=GetTickCount64();if(logNow-logWindow>=1000){logWindow=logNow;logCount=0;}if(logCount++>=24){ReleaseSRWLockExclusive(&logLock);return;}
 if(!_wfopen_s(&f,logPath.c_str(),L"a") && f) {
  SYSTEMTIME t;GetLocalTime(&t);fprintf(f,"[%02u:%02u:%02u] ",t.wHour,t.wMinute,t.wSecond);
  va_list a;va_start(a,fmt);vfprintf(f,fmt,a);va_end(a);fputc('\n',f);fclose(f);
} ReleaseSRWLockExclusive(&logLock);
}
struct LootProfile {
 const char* label;uint64_t until=0,count=0;LONGLONG total=0,maximum=0,frequency=0;bool done=false;
 bool Begin(LARGE_INTEGER& start){if(done)return false;if(!until){until=GetTickCount64()+5000;LARGE_INTEGER f;QueryPerformanceFrequency(&f);frequency=f.QuadPart;}QueryPerformanceCounter(&start);return true;}
 void End(const LARGE_INTEGER& start){LARGE_INTEGER end;QueryPerformanceCounter(&end);auto elapsed=end.QuadPart-start.QuadPart;total+=elapsed;maximum=(std::max)(maximum,elapsed);++count;if(GetTickCount64()>=until){done=true;Log("LOOT_PROFILE stage=%s samples=%llu average_us=%.2f maximum_us=%.2f duration_ms=5000",label,(unsigned long long)count,1000000.0*total/frequency/count,1000000.0*maximum/frequency);}}
};
template<class T> static T Read(const void* p,size_t off=0){T v;memcpy(&v,(const uint8_t*)p+off,sizeof v);return v;}
static uint8_t* Unique(const char* pattern) {
 std::vector<int> b; const char* p=pattern;
 while(*p){if(*p==' '){++p;continue;}if(*p=='?'){b.push_back(-1);++p;if(*p=='?')++p;}else{char s[3]={p[0],p[1],0};b.push_back(strtoul(s,nullptr,16));p+=2;}}
 uint8_t* result=nullptr;int n=0;
 for(size_t i=0;i+b.size()<=textSize;++i){if(b[0]>=0 && textStart[i]!=b[0])continue;bool ok=true;for(size_t j=1;j<b.size();++j)if(b[j]>=0&&textStart[i+j]!=b[j]){ok=false;break;}if(ok){result=textStart+i;++n;}}
 return n==1?result:nullptr;
}
static bool InText(const uint8_t* p){return p>=textStart&&p<textStart+textSize;}
static uint8_t* Rel(const uint8_t* p,size_t instructionSize,size_t disp){return (uint8_t*)p+instructionSize+Read<int32_t>(p,disp);}
using GetFn=void*(__fastcall*)(void*,void*,uint32_t,uint32_t);
static GetFn getRead=nullptr,getMutable=nullptr;
static size_t registryOffset=0,entityMapOffset=0;

// Hash-map layout independently checked against the game's generated lookup code
// during installation. Component field layouts come from game_abi.h.
struct MapView {uint64_t reserved;uint64_t* occupied;uint64_t capacity;uint8_t reserved2[16];uint32_t* keys;uint8_t reserved3[16];void** values;uint8_t reserved4[12];uint32_t count;};
static_assert(offsetof(MapView,occupied)==8 && offsetof(MapView,capacity)==16);
static_assert(offsetof(MapView,keys)==40 && offsetof(MapView,values)==64 && offsetof(MapView,count)==84);
static void* Component(void* world,uint32_t hash,uint32_t id){alignas(16) uint8_t result[32]={}; getRead(result,world,hash,id);return Read<void*>(result);}

static double pickupRadius=3.0;
static loot_config::Menu pickupMenu;static bool menuReady=false;
// Temporary safety interlock while context targeting is under investigation.
// F8 automatic mode must not silently restore untargeted interactions.
static bool automationPaused=true;
static bool targetedCorpseAutoLoot=false;
static uint64_t radiusCheck=0;
static void RefreshRadius(){
 auto now=GetTickCount64();if(radiusCheck&&now-radiusCheck<250)return;radiusCheck=now;
 const bool paused=GetPrivateProfileIntW(L"Safety",L"PauseAutomaticActions",1,ini.c_str())!=0;
 static int previousPause=-1;
 if(previousPause!=int(paused)){Log("AUTOMATION_SAFETY_PAUSE enabled=%u",paused);previousPause=paused;}
 automationPaused=paused;
 targetedCorpseAutoLoot=GetPrivateProfileIntW(L"Safety",L"TargetedCorpseAutoLoot",0,ini.c_str())!=0;
 FILE* file=nullptr;const auto path=dir+L"\\mods\\auto-loot-plus\\config.txt";
 if(_wfopen_s(&file,path.c_str(),L"r")||!file)return;
 auto next=pickupMenu;const bool valid=loot_config::ReadMenu(file,next);fclose(file);if(!valid)return;
 static std::string previous;char state[512];sprintf_s(state,"enabled=%u auto=%u radius=%.3f ground=%u stones=%u collectibles=%u plants=%u farm=%u corpses=%u chests=%u instant=%u sight=%u smash=%u buildable=%u stack=%u backpack=%u touch=%u debug=%u",next.enabled,next.automatic,next.radius,next.ground,next.stones,next.collectibles,next.plants,next.farm,next.corpses,next.chests,next.instant,next.sight,next.smash,next.buildable,next.stack,next.backpack,next.touchZones,next.debug);
 if(previous!=state){Log("MENU_POLICY %s",state);previous=state;}
 next.animalProduce=GetPrivateProfileIntW(L"Safety",L"CollectAnimalProduce",1,ini.c_str())==1;
 pickupMenu=next;pickupRadius=next.radius;menuReady=true;
}
static size_t templateMember=0;static bool templateChecked=false;
static const TemplatePolicy* TemplateForRecord(void* record){
 if(!templateChecked){templateChecked=true;auto p=Unique("4D 8B 0C FF 49 8B 41 ?? 80 78 20 00 0F 84");if(p&&p[7]>=32&&p[7]<=64&&p[7]%8==0){templateMember=p[7];Log("TEMPLATE_MEMBER_CHECKED field=%zu",templateMember);}else Log("TEMPLATE_MEMBER_UNSUPPORTED automatic_unknown_targets_blocked=1");}
 if(!templateMember||!record)return nullptr;
 auto descriptor=Read<void*>(record,templateMember);if(!descriptor)return nullptr;
 return FindTemplatePolicy(Read<uint64_t>(descriptor),Read<uint64_t>(descriptor,8));
}
static bool LivingTameAnimal(void* world,uint32_t id){auto health=Component(world,abi::NetworkHealth::hash,id);return Component(world,0x1399e0bau,id)&&health&&Read<uint32_t>(health,abi::NetworkHealth::healthMax)>0&&Read<uint32_t>(health,abi::NetworkHealth::health)>0;}
static bool AnimalProduceReady(void* world,uint32_t id,uint32_t verb){
 auto offer=Component(world,abi::Offer::hash,id);
 return verb&&LivingTameAnimal(world,id)&&offer&&Read<uint8_t>(offer,abi::Offer::isOffered)&&Read<uint32_t>(offer,abi::Offer::verbId)==verb;
}
static loot_config::Category CategoryForRecord(void* world,uint32_t id,void* record){
 auto info=TemplateForRecord(record);if(info&&(info->category==loot_config::Category::Blocked||loot_config::PetTemplate(info->name)))return loot_config::Category::Blocked;
 auto actor=Component(world,abi::NetworkActor::hash,id);auto health=Component(world,abi::NetworkHealth::hash,id);
 if(actor&&health&&Read<uint32_t>(health,abi::NetworkHealth::healthMax)>0&&Read<uint32_t>(health,abi::NetworkHealth::health)==0)return loot_config::Category::Corpse;
 if(info&&info->category==loot_config::Category::AnimalProduce&&!AnimalProduceReady(world,id,info->produceVerb))return loot_config::Category::Blocked;
 return info?info->category:loot_config::Category::Unknown;
}

struct Seen {loot_config::Category category=loot_config::Category::Unknown;uint32_t entity=0,item=0,count=0,attempts=0,openings=0;uint64_t lastSeen=0,lastRequest=0,lastOpen=0,poseRetryAt=0,lastAttempt=0;bool wasPopulated=false,depleted=false;uint32_t produceVerb=0;double distance2=HUGE_VAL;};
static Seen seen[1024];
static uint64_t lastScan=0,lastSummary=0,scans=0,requests=0;
static loot_config::Category CachedCategory(uint32_t id){for(const auto& item:seen)if(item.entity==id&&GetTickCount64()-item.lastSeen<500)return item.category;return loot_config::Category::Unknown;}
static bool DirectedLootTarget(void* world,uint32_t id,bool resources){auto c=CachedCategory(id);if(c==loot_config::Category::AnimalProduce){for(const auto& entry:seen)if(entry.entity==id)return resources&&loot_config::Allowed(pickupMenu,c)&&AnimalProduceReady(world,id,entry.produceVerb);return false;}return loot_config::Allowed(pickupMenu,c)&&(c==loot_config::Category::Corpse||(resources&&loot_config::ResourceCategory(c)));}

static double Distance2(void* a,void* b){double s=0;for(int i=0;i<3;++i){double d=((double)Read<int64_t>(a,i*8)-(double)Read<int64_t>(b,i*8))/4294967296.0;s+=d*d;}return s;}
static bool GameForeground(){DWORD pid=0;GetWindowThreadProcessId(GetForegroundWindow(),&pid);return pid==GetCurrentProcessId();}
static bool CursorVisible(){CURSORINFO ci={sizeof ci};return GetCursorInfo(&ci)&&(ci.flags&CURSOR_SHOWING);}
static Seen& Track(uint32_t id){Seen* oldest=&seen[0];for(auto& s:seen){if(s.entity==id)return s;if(s.lastSeen<oldest->lastSeen)oldest=&s;}*oldest={};oldest->entity=id;return *oldest;}

using InventoryCounts=std::map<uint32_t,uint64_t>;
static InventoryCounts InventorySnapshot(void* world,uint32_t player){
 InventoryCounts items;std::vector<uint32_t> ids{player};
 for(size_t i=0;i<ids.size()&&i<64;++i){auto id=ids[i];auto inv=(uint8_t*)Component(world,abi::Inventory::hash,id);if(inv)for(size_t j=0;j<8;++j){auto stack=inv+j*abi::Stack::size;auto item=Read<uint32_t>(stack);auto count=Read<uint32_t>(stack,abi::Stack::data+abi::ItemData::count);if(item&&count)items[item]+=count;}
 auto setup=(uint8_t*)Component(world,abi::InventorySetup::hash,id);if(setup)for(size_t j=0;j<16;++j){auto next=Read<uint32_t>(setup,abi::InventorySetup::linksEntities+j*4);if(next&&std::find(ids.begin(),ids.end(),next)==ids.end()&&ids.size()<64)ids.push_back(next);}}
 return items;
}
struct InventorySummary {uint32_t amount=0,firstItem=0;bool forbidden=false;};
static InventorySummary SummarizeInventory(void* world,uint32_t source){
 InventorySummary summary;
 for(auto [item,count]:InventorySnapshot(world,source))if(count){
  if(!summary.firstItem)summary.firstItem=item;
  summary.amount=uint32_t((std::min)(uint64_t(UINT32_MAX),uint64_t(summary.amount)+count));
  if(std::binary_search(questItems.begin(),questItems.end(),item))summary.forbidden=true;
 }
 return summary;
}
static InventoryCounts beforePickup;static uint64_t verifyAt=0;static void* verifyWorld=nullptr;static uint32_t verifyPlayer=0;
static uint32_t nearPick=0,unsafeOffers=0,nearLoot=0;static double nearPickDistance=0;
static uint32_t nearDirectCorpse=0;
static uint32_t nearColdCorpse=0,nearSafeResource=0,coldOpeningBlockers=0,protectedOffers=0;
static bool directedTestActive=false;
static void Scan(void* world,uint32_t player,uint8_t* input) {
 static void* trackedWorld=nullptr;static uint32_t trackedPlayer=0;
 if(world!=trackedWorld||player!=trackedPlayer){for(auto& entry:seen)entry={};trackedWorld=world;trackedPlayer=player;lastScan=0;}
 uint64_t now=GetTickCount64();if(now-lastScan<corpse_transfer::ScanMs)return;lastScan=now;
 const auto started=GetTickCount64();
 if(verifyAt&&now>=verifyAt&&world==verifyWorld&&player==verifyPlayer){auto after=InventorySnapshot(world,player);unsigned gains=0;for(auto [item,count]:after){auto previous=beforePickup[item];if(count>previous){++gains;Log("INVENTORY_GAIN item=%u before=%llu after=%llu",item,(unsigned long long)previous,(unsigned long long)count);}}if(gains){++ClientLootRuntimeStatus.gainBatches;ClientLootRuntimeStatus.gainTypes+=gains;}Log("PICKUP_VERIFICATION gains=%u",gains);verifyAt=0;}
 RefreshRadius();const double radius2=pickupRadius*pickupRadius;
 // Changing the slider starts a fresh bounded attempt window. A target that
 // exhausted retries while out of reach must not remain suppressed afterward.
 static double previousRadius=pickupRadius;
 if(previousRadius!=pickupRadius){for(auto& tracked:seen){tracked.attempts=0;tracked.lastRequest=0;}previousRadius=pickupRadius;}
 static uint32_t snapshotSequence=0;const auto snapshot=GetPrivateProfileIntW(L"Testing",L"SnapshotOnce",0,ini.c_str());const bool snapshotNow=snapshot&&snapshot!=snapshotSequence;if(snapshotNow)snapshotSequence=snapshot;
 nearPick=0;unsafeOffers=0;nearLoot=0;nearDirectCorpse=0;nearColdCorpse=0;nearSafeResource=0;coldOpeningBlockers=0;protectedOffers=0;double pickD2=radius2,lootD2=radius2,directCorpseD2=radius2,coldCorpseD2=radius2,resourceD2=radius2,excludedD2=HUGE_VAL;
 uint64_t coldSelectedLast=UINT64_MAX,resourceSelectedLast=UINT64_MAX;

 void* pos=Component(world,abi::WorldTransform::hash,player);
 if(!pos){if(now-lastSummary>5000){lastSummary=now;Log("NO_PLAYER_TRANSFORM player=%u",player);}return;}
 auto map=(MapView*)((uint8_t*)world+registryOffset+entityMapOffset);
 if(map->capacity>1048576||map->count>map->capacity||!map->occupied||!map->keys||!map->values){disabled=true;Log("INVALID_ENTITY_MAP; disabled");return;}
 unsigned pickups=0,nearby=0,withInventory=0,totalInventories=0,totalOffers=0,transforms=0; uint32_t candidate=0;
 const uint32_t testTarget=GetPrivateProfileIntW(L"Testing",L"TargetEntity",0,ini.c_str());
 for(size_t n=0;n<map->capacity;++n) {
  if(!(map->occupied[n/64]&(1ull<<(n%64))))continue;
  const uint32_t id=map->keys[n];if(!id||id==player||!map->values[n])continue;
  auto pickup=(uint8_t*)Component(world,abi::Pickup::hash,id);if(pickup)++pickups;
  auto inv=(uint8_t*)Component(world,abi::Inventory::hash,id);
  auto offer=(uint8_t*)Component(world,abi::Offer::hash,id);
  if(inv)++totalInventories;if(offer)++totalOffers;
  if(!pickup&&!inv&&!offer)continue;
  auto p=Component(world,abi::WorldTransform::hash,id);if(p)++transforms;if(!p)continue;
  const double d2=Distance2(pos,p);if(d2>(std::max)(64.0,radius2))continue;++nearby;
  auto& retry=Track(id);const auto category=CategoryForRecord(world,id,map->values[n]);const bool categoryAllowed=menuReady&&loot_config::Allowed(pickupMenu,category);retry.category=category;
  if(category==loot_config::Category::AnimalProduce||category==loot_config::Category::Blocked){auto policy=TemplateForRecord(map->values[n]);retry.produceVerb=policy?policy->produceVerb:0;}else retry.produceVerb=0;
  if(snapshotNow||now-retry.lastSeen>3000){const auto info=TemplateForRecord(map->values[n]);Log("TARGET_POLICY id=%u name=%s category=%u allowed=%u distance=%.2f",id,info?info->name:"<unknown>",unsigned(category),categoryAllowed,sqrt(d2));}
  if(now-retry.lastSeen>10000||(retry.lastRequest&&now-retry.lastRequest>5000))retry.attempts=0;
  if(now-retry.lastSeen>10000)retry.openings=0;
  if(offer&&Read<uint8_t>(offer,abi::Offer::isOffered)&&d2<=(std::max)(9.0,radius2)){
   const auto verb=Read<uint32_t>(offer,abi::Offer::verbId);const bool resourceVerb=verb==48836331u||verb==1577711826u||verb==2425743549u;
   if(!categoryAllowed){++unsafeOffers;excludedD2=(std::min)(excludedD2,d2);}
   else if(resourceVerb&&retry.attempts<12&&d2<=pickD2){nearPick=id;pickD2=d2;nearPickDistance=sqrt(d2);}
   else if(loot_config::UnsupportedContextVerb(category,verb)&&d2<=9.0){++unsafeOffers;excludedD2=(std::min)(excludedD2,d2);}
  }

  auto setup=(uint8_t*)Component(world,abi::InventorySetup::hash,id);
  if(offer&&Read<uint8_t>(offer,abi::Offer::isOffered)&&corpse_transfer::BlocksOpening(categoryAllowed,!loot_config::UnsupportedContextVerb(category,Read<uint32_t>(offer)),d2,pickupRadius)){if(loot_config::RequiresTargetConfirmation(category,categoryAllowed))++protectedOffers;else ++coldOpeningBlockers;}
  if(snapshotNow)Log("TARGET_STATUS id=%u category=%u allowed=%u distance=%.2f inventory=%u initialized=%u offered=%u verb=%u attempts=%u last_accept=%u",id,unsigned(category),categoryAllowed,sqrt(d2),inv!=nullptr,setup?Read<uint8_t>(setup,abi::InventorySetup::isInitialized):0,offer?Read<uint8_t>(offer,abi::Offer::isOffered):0,offer?Read<uint32_t>(offer):0,retry.attempts,offer?Read<uint32_t>(offer,abi::Offer::lastAcceptionId):0);
  if(snapshotNow)Log("RESOURCE_GATE id=%u resource=%u allowed=%u offered=%u now=%llu pose_retry=%llu last_open=%llu openings=%u",id,loot_config::ResourceCategory(category),categoryAllowed,offer?Read<uint8_t>(offer,abi::Offer::isOffered):0,(unsigned long long)now,(unsigned long long)retry.poseRetryAt,(unsigned long long)retry.lastOpen,retry.openings);
  uint32_t amount=0,firstItem=0;bool forbidden=false;
  if(inv&&setup&&Read<uint8_t>(setup,abi::InventorySetup::isInitialized)){
   ++withInventory;
   if(category==loot_config::Category::Corpse||category==loot_config::Category::Chest){const auto summary=SummarizeInventory(world,id);amount=summary.amount;firstItem=summary.firstItem;forbidden=summary.forbidden;}
   else for(size_t s=0;s<8;++s){auto stack=inv+s*abi::Stack::size;auto item=Read<uint32_t>(stack,abi::Stack::id);auto count=Read<uint32_t>(stack,abi::Stack::data+abi::ItemData::count);if(item&&count){if(!firstItem)firstItem=item;amount+=count;if(std::binary_search(questItems.begin(),questItems.end(),item))forbidden=true;}}
  }
  if(categoryAllowed&&(category==loot_config::Category::Corpse||category==loot_config::Category::Chest)&&retry.attempts<3&&!forbidden&&inv&&setup&&Read<uint8_t>(setup,abi::InventorySetup::isInitialized)&&offer&&Read<uint8_t>(offer,abi::Offer::isOffered)&&d2<=lootD2&&(Read<uint32_t>(offer)==183940555u||Read<uint32_t>(offer)==2589564171u)){nearLoot=id;lootD2=d2;}
  const corpse_transfer::Candidate directCandidate{category==loot_config::Category::Corpse,categoryAllowed,inv!=nullptr,setup&&Read<uint8_t>(setup,abi::InventorySetup::isInitialized),forbidden,amount,retry.attempts,retry.lastRequest};
  if(amount){retry.wasPopulated=true;retry.depleted=false;retry.openings=0;}else if(retry.wasPopulated)retry.depleted=true;
  if(d2<=directCorpseD2&&corpse_transfer::Eligible(directCandidate,now)){nearDirectCorpse=id;directCorpseD2=d2;}
  if(now>=retry.poseRetryAt&&d2<=radius2&&corpse_transfer::NeedsOpening(directCandidate,offer&&Read<uint8_t>(offer,abi::Offer::isOffered),retry.lastOpen,now,retry.openings,retry.depleted)&&corpse_transfer::PreferOpening(retry.lastAttempt,d2,coldSelectedLast,coldCorpseD2)){nearColdCorpse=id;coldCorpseD2=d2;coldSelectedLast=retry.lastAttempt;}
  if(now>=retry.poseRetryAt&&d2<=radius2&&corpse_transfer::ResourceEligible(loot_config::ResourceCategory(category),categoryAllowed,offer&&Read<uint8_t>(offer,abi::Offer::isOffered),forbidden,retry.lastOpen,now,retry.openings)&&corpse_transfer::PreferOpening(retry.lastAttempt,d2,resourceSelectedLast,resourceD2)){nearSafeResource=id;resourceD2=d2;resourceSelectedLast=retry.lastAttempt;}
  auto& entry=Track(id);
  if(now-entry.lastSeen>3000||entry.item!=firstItem||entry.count!=amount){auto hp=Component(world,abi::NetworkHealth::hash,id);auto family=Component(world,abi::FamilyInfo::hash,id);Log("TARGET_CLASS id=%u hp_present=%u hp=%u hpmax=%u family_present=%u family=%u",id,hp!=nullptr,hp?Read<uint32_t>(hp,abi::NetworkHealth::health):0,hp?Read<uint32_t>(hp,abi::NetworkHealth::healthMax):0,family!=nullptr,family?Read<uint8_t>(family):0);}

  if(now-entry.lastSeen>3000||entry.item!=firstItem||entry.count!=amount)
   Log("NEAR id=%u dist=%.2f pickup=%d pickup_item=%u pickup_amount=%u inventory=%d initialized=%d slots_count=%u first_item=%u offer=%d offered=%d verb=%u",id,sqrt(d2),pickup!=nullptr,pickup?Read<uint32_t>(pickup,abi::Pickup::itemId):0,pickup?Read<uint16_t>(pickup,abi::Pickup::pickupAmount):0,inv!=nullptr,setup?Read<uint8_t>(setup,abi::InventorySetup::isInitialized):0,amount,firstItem,offer!=nullptr,offer?Read<uint8_t>(offer,abi::Offer::isOffered):0,offer?Read<uint32_t>(offer):0);
  if(now-entry.lastSeen>10000)entry.attempts=0;
  entry.lastSeen=now;entry.item=firstItem;entry.count=amount;entry.distance2=d2;
  // Diagnostic builds never send requests until explicitly enabled. Candidate
  // gating will be tightened after observing ordinary replicated inventory data.
  if((pickup||id==testTarget)&&inv&&setup&&amount&&!forbidden&&offer&&Read<uint8_t>(offer,abi::Offer::isOffered)&&entry.attempts<3&&now-entry.lastRequest>5000&&!candidate)candidate=id;
 }
 const double candidateD2=(std::max)(nearPick?pickD2:0.0,nearLoot?lootD2:0.0);
 if(unsafeOffers&&!loot_config::ContextBlocked(excludedD2,candidateD2))unsafeOffers=0;
 if(snapshotNow)Log("SNAPSHOT_SELECTION pick=%u loot=%u unsafe=%u excluded_distance=%.2f candidate_distance=%.2f",nearPick,nearLoot,unsafeOffers,sqrt(excludedD2),sqrt(candidateD2));
 if(snapshotNow){auto query=Component(world,0x2a0c1e34,player);auto actor=Component(world,abi::NetworkActor::hash,player);Log("PASSIVE_INTERACTION_STATE query_value=%u query_special=%u network_host=%llu sequence=%u",query?Read<uint32_t>(query):0,query?Read<uint8_t>(query,4):0,actor?Read<unsigned long long>(actor,abi::NetworkActor::interactionHostId):0,actor?Read<uint32_t>(actor,abi::NetworkActor::sequenceRuntimeId+abi::SequenceRuntime::sequenceId):0);}
 ++scans;
 if(now-lastSummary>5000){
  auto q=Component(world,0x2a0c1e34,player);auto qid=q?Read<uint32_t>(q):0;
  auto qp=qid?Component(world,abi::WorldTransform::hash,qid):nullptr;
  auto qo=qid?Component(world,abi::Offer::hash,qid):nullptr;
  auto qi=qid?Component(world,abi::Inventory::hash,qid):nullptr;
  Log("DETAIL world=%p input=%p player_pos=%p xyz=%lld,%lld,%lld query=%u query_pos=%p inv=%p offer=%p verb=%u transforms=%u",world,input,pos,Read<long long>(pos),Read<long long>(pos,8),Read<long long>(pos,16),qid,qp,qi,qo,qo?Read<uint32_t>(qo):0,transforms);
  if(qp)Log("QUERY_POSITION xyz=%lld,%lld,%lld distance=%.3f",Read<long long>(qp),Read<long long>(qp,8),Read<long long>(qp,16),sqrt(Distance2(pos,qp)));
 }

 if(now-lastSummary>5000){lastSummary=now;Log("SCAN player=%u entities=%u capacity=%llu pickups=%u all_inventories=%u all_offers=%u nearby=%u inventories=%u elapsed_ms=%llu scans=%llu requests=%llu cursor=%d",player,map->count,(unsigned long long)map->capacity,pickups,totalInventories,totalOffers,nearby,withInventory,(unsigned long long)(GetTickCount64()-started),(unsigned long long)scans,(unsigned long long)requests,CursorVisible());}
 if(candidate && !CursorVisible() && GetPrivateProfileIntW(L"Testing",L"RequestTransfers",0,ini.c_str())==1) {
  auto state=(uint8_t*)Component(world,abi::PlayerState::hash,player);
  // Respect normal gameplay/menu state as well as the visible OS cursor.
  if(state&&(Read<uint32_t>(state,abi::PlayerState::playerStateMask)&0x80)) {
   auto action=input+abi::Input::data+abi::InputData::inventoryTransferAction;
   uint32_t version=Read<uint32_t>(action,abi::Transfer::versionData)+1;
   memset(action,0,abi::Transfer::size);
   memcpy(action+abi::Transfer::versionData,&version,sizeof version);
   memcpy(action+abi::Transfer::sourceEntityId,&candidate,sizeof candidate);
   memcpy(action+abi::Transfer::targetEntityId,&player,sizeof player);
   action[abi::Transfer::type]=2; // InventoryActionType.All (actual game enum).
   auto& s=Track(candidate);s.lastRequest=now;++s.attempts;++requests;
   Log("REQUEST_TRANSFER source=%u target=%u version=%u item=%u count=%u attempt=%u",candidate,player,version,s.item,s.count,s.attempts);
  }
 }
}
static void ProbeSafe(void* world,uint32_t player,uint8_t* input){static LootProfile profile{"scan"};LARGE_INTEGER start;const bool sample=profile.Begin(start);__try{Scan(world,player,input);}__except(EXCEPTION_EXECUTE_HANDLER){disabled=true;Log("PROBE_EXCEPTION code=%08lx; disabled",GetExceptionCode());}if(sample)profile.End(start);}
using StepFn=void(__cdecl*)(void*,uint32_t,uint8_t*,GetFn,size_t,size_t);
static StepFn devStep=nullptr;static std::wstring devPath;static uint64_t devCheck=0;
static void DevelopmentStep(void* world,uint32_t player,uint8_t* input){
 if(GetTickCount64()-devCheck>1000){devCheck=GetTickCount64();wchar_t path[1024];GetPrivateProfileStringW(L"Diagnostics",L"Module",L"",path,1024,ini.c_str());
  if(path[0]&&devPath!=path){HMODULE m=LoadLibraryW(path);auto step=m?(StepFn)GetProcAddress(m,"ProbeStep"):nullptr;if(step){devStep=step;devPath=path;Log("DEVELOPMENT_MODULE_LOADED");}else Log("DEVELOPMENT_MODULE_FAILED error=%lu",GetLastError());}
 }
 if(devStep)devStep(world,player,input,getRead,registryOffset,entityMapOffset);else ProbeSafe(world,player,input);
}
static void* AllocNear(void*,size_t);
static GetFn lateOriginal=nullptr;
static uint32_t contextSequence=0;static uint64_t contextUntil=0;static uint32_t contextPlayer=0;
static void* __fastcall OnLateInput(void* out,void* world,uint32_t hash,uint32_t player){
 void* result=lateOriginal(out,world,hash,player);auto input=Read<uint8_t*>(out);if(!input)return result;
 static uint64_t oldBits=~0ull;auto newBits=Read<uint64_t>(input,abi::InputData::digitalInput);
 if(newBits!=oldBits){Log("DIGITAL_LATE player=%u value=%llx mask=%x",player,(unsigned long long)newBits,Read<uint16_t>(input,abi::InputData::mask));oldBits=newBits;}
 auto seq=GetPrivateProfileIntW(L"Testing",L"ContextOnce",0,ini.c_str());
 if(seq&&seq!=contextSequence&&!CursorVisible()){
  contextSequence=seq;contextUntil=GetTickCount64()+200;contextPlayer=player;
  Log("CONTEXT_TEST_BEGIN player=%u sequence=%u original=%llx",player,seq,Read<unsigned long long>(input,abi::InputData::digitalInput));
 }
 if(player==contextPlayer&&GetTickCount64()<contextUntil){
  auto bits=Read<uint64_t>(input,abi::InputData::digitalInput)|(contextUntil-GetTickCount64()>100?0x20ull:0x40ull);memcpy(input+abi::InputData::digitalInput,&bits,8);
 }
 return result;
}
static void InstallLate(){
 if(lateOriginal)return;
 if(!textStart){auto base=(uint8_t*)GetModuleHandleW(nullptr);auto nt=(IMAGE_NT_HEADERS64*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);auto sec=IMAGE_FIRST_SECTION(nt);for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i,++sec)if(!memcmp(sec->Name,".text",5)){textStart=base+sec->VirtualAddress;textSize=sec->Misc.VirtualSize;}}
 auto p=Unique("44 8B 05 ?? ?? ?? ?? 48 8D 8C 24 E8 00 00 00 48 89 84 24 80 01 00 00 E8 ?? ?? ?? ?? 44 8B 4E 04");
 if(!p||Read<uint32_t>(Rel(p,7,3))!=abi::Input::hash){Log("LATE_SITE_NOT_SUPPORTED");return;}
 auto call=p+23;auto original=Unique("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 20 57 48 83 EC 30 48 8B E9 44 89 44 24 50 48 8B 8A ?? ?? ?? ?? 48 8B F2 48 81 C1 88 00 00 00 48 8D 54 24 50 41 8B D9 E8 ?? ?? ?? ?? 4C 8B C8");if(!original||Read<uint32_t>(original,31)!=registryOffset)return;
 auto stub=(uint8_t*)AllocNear(call,64);if(!stub)return;
 stub[0]=0x48;stub[1]=0xb8;uint64_t fn=(uint64_t)&OnLateInput;memcpy(stub+2,&fn,8);stub[10]=0xff;stub[11]=0xe0;
 intptr_t displacement=stub-(call+5);if(displacement<INT32_MIN||displacement>INT32_MAX)return;
 DWORD old;if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old))return;
 lateOriginal=(GetFn)original;int32_t rel=(int32_t)displacement;memcpy(call+1,&rel,4);DWORD ignored;VirtualProtect(call,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),call,5);Log("LATE_INPUT_SITE_INSTALLED context_tests_require_ini_sequence=1");
}
static volatile LONG64 lastRecenter=0;using SetPosFn=BOOL(WINAPI*)(int,int);static SetPosFn previousSetPos=nullptr;
static BOOL WINAPI ObserveSetPos(int x,int y){InterlockedExchange64(&lastRecenter,(LONG64)GetTickCount64());return previousSetPos(x,y);}
extern "C" __declspec(dllexport) unsigned long long __cdecl ClientCursorAge(){auto t=(uint64_t)InterlockedCompareExchange64(&lastRecenter,0,0);return t?GetTickCount64()-t:999999;}
static void InstallCursorProbe(){if(previousSetPos)return;auto base=(uint8_t*)GetModuleHandleW(nullptr);auto nt=(IMAGE_NT_HEADERS64*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);auto d=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!d.VirtualAddress)return;
 for(auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+d.VirtualAddress);desc->Name;++desc){if(!desc->OriginalFirstThunk)continue;auto named=(IMAGE_THUNK_DATA64*)(base+desc->OriginalFirstThunk);auto slot=(IMAGE_THUNK_DATA64*)(base+desc->FirstThunk);
  for(;named->u1.AddressOfData;++named,++slot){if(IMAGE_SNAP_BY_ORDINAL64(named->u1.Ordinal))continue;auto import=(IMAGE_IMPORT_BY_NAME*)(base+named->u1.AddressOfData);if(strcmp((char*)import->Name,"SetCursorPos"))continue;DWORD old;if(!VirtualProtect(&slot->u1.Function,8,PAGE_READWRITE,&old))continue;previousSetPos=(SetPosFn)slot->u1.Function;slot->u1.Function=(ULONGLONG)&ObserveSetPos;DWORD ignored;VirtualProtect(&slot->u1.Function,8,old,&ignored);Log("CURSOR_CAPTURE_PROBE_INSTALLED");return;}}
}
using UiCheckFn=bool(__fastcall*)(void*);static UiCheckFn uiOriginal=nullptr;static void* uiApp=nullptr;static uint64_t uiAt=0;static bool uiValue=true;
static bool __fastcall ObserveUi(void* app){bool b=uiOriginal(app);uiApp=app;uiAt=GetTickCount64();if(b!=uiValue){uiValue=b;Log("GAME_INPUT_BLOCK changed=%d",b);}return b;}
extern "C" __declspec(dllexport) bool __cdecl ClientMenusBlocked(){return !uiApp||GetTickCount64()-uiAt>250||uiOriginal(uiApp);}
using ExternalMenuFn=bool(__cdecl*)();static ExternalMenuFn externalMenus=nullptr;
extern "C" __declspec(dllexport) bool ClientUiGuardOwner(){return uiOriginal!=nullptr;}
static HMODULE PriorGuardOwner(const char* ownerExport){
 auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPMODULE,GetCurrentProcessId());if(snapshot==INVALID_HANDLE_VALUE)return nullptr;
 MODULEENTRY32W entry={sizeof entry};HMODULE result=nullptr;
 if(Module32FirstW(snapshot,&entry))do{auto owner=(ExternalMenuFn)GetProcAddress(entry.hModule,ownerExport);if(owner&&owner()){result=entry.hModule;break;}}while(Module32NextW(snapshot,&entry));
 CloseHandle(snapshot);return result;
}

static void InstallUiProbe(){
 if(!externalMenus){auto m=PriorGuardOwner("ClientUiGuardOwner");if(!m)m=GetModuleHandleW(L"Probe16.dll");if(!m)m=GetModuleHandleW(L"Probe36.dll");if(!m)m=GetModuleHandleW(L"Probe52.dll");if(m)externalMenus=(ExternalMenuFn)GetProcAddress(m,"ClientMenusBlocked");}
 if(externalMenus||uiOriginal)return;
 auto p=Unique("4C 8B BD 90 00 00 00 48 8B F1 49 8B CF E8 ?? ?? ?? ?? 33 FF 84 C0 74 51 0F 57 C0 48 89 BE A8 03 00 00");if(!p){Log("UI_GUARD_SIGNATURE_UNSUPPORTED");return;}
 auto call=p+13;auto original=Rel(call,5,1);const uint8_t a[]={0x80,0xb9};const uint8_t z[]={0x01,0x0f,0x94,0xc0,0xc3};if(!InText(original)||memcmp(original,a,2)||memcmp(original+6,z,5))return;
 auto stub=(uint8_t*)AllocNear(call,64);if(!stub)return;stub[0]=0x48;stub[1]=0xb8;uint64_t fn=(uint64_t)&ObserveUi;memcpy(stub+2,&fn,8);stub[10]=0xff;stub[11]=0xe0;intptr_t displacement=stub-(call+5);if(displacement<INT32_MIN||displacement>INT32_MAX)return;
 DWORD old;if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old))return;uiOriginal=(UiCheckFn)original;int32_t rel=(int32_t)displacement;memcpy(call+1,&rel,4);DWORD ignored;VirtualProtect(call,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),call,5);Log("GAME_INPUT_GUARD_INSTALLED");
}
using CapturedFn=bool(__cdecl*)();static CapturedFn existingCapture=nullptr;
extern "C" __declspec(dllexport) bool __cdecl ClientCursorCaptured();
static bool MenusBlocked(){
 static auto overlay=GetModuleHandleW(L"AutoLootPlus.asi");
 static auto menuOpen=overlay?(CapturedFn)GetProcAddress(overlay,"AutoLootPlusMenuOpen"):nullptr;
 if(menuOpen&&menuOpen())return true;
 return (externalMenus?externalMenus():ClientMenusBlocked())||!(existingCapture?existingCapture():ClientCursorCaptured());}
using ClipFn=BOOL(WINAPI*)(const RECT*);static ClipFn previousClip=nullptr;static bool clipKnown=false,clipRequested=false;
extern "C" __declspec(dllexport) bool ClientCaptureGuardOwner(){return previousClip!=nullptr;}
static BOOL WINAPI ObserveClip(const RECT* rect){if(!clipKnown||clipRequested!=(rect!=nullptr)){clipKnown=true;clipRequested=rect!=nullptr;Log("MOUSE_CAPTURE requested=%d rectangle=%ld,%ld,%ld,%ld",clipRequested,rect?rect->left:0,rect?rect->top:0,rect?rect->right:0,rect?rect->bottom:0);}return previousClip(rect);}
extern "C" __declspec(dllexport) bool __cdecl ClientCursorCaptured(){return clipKnown&&clipRequested;}
static void InstallClipProbe(){if(previousClip||existingCapture)return;auto prior=PriorGuardOwner("ClientCaptureGuardOwner");if(!prior)prior=GetModuleHandleW(L"Probe18.dll");if(!prior)prior=GetModuleHandleW(L"Probe36.dll");if(!prior)prior=GetModuleHandleW(L"Probe52.dll");if(prior)existingCapture=(CapturedFn)GetProcAddress(prior,"ClientCursorCaptured");if(existingCapture)return;auto base=(uint8_t*)GetModuleHandleW(nullptr);auto nt=(IMAGE_NT_HEADERS64*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);auto d=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!d.VirtualAddress)return;
 for(auto desc=(IMAGE_IMPORT_DESCRIPTOR*)(base+d.VirtualAddress);desc->Name;++desc){if(!desc->OriginalFirstThunk)continue;auto named=(IMAGE_THUNK_DATA64*)(base+desc->OriginalFirstThunk);auto slot=(IMAGE_THUNK_DATA64*)(base+desc->FirstThunk);for(;named->u1.AddressOfData;++named,++slot){if(IMAGE_SNAP_BY_ORDINAL64(named->u1.Ordinal))continue;auto import=(IMAGE_IMPORT_BY_NAME*)(base+named->u1.AddressOfData);if(strcmp((char*)import->Name,"ClipCursor"))continue;DWORD old;if(!VirtualProtect(&slot->u1.Function,8,PAGE_READWRITE,&old))continue;previousClip=(ClipFn)slot->u1.Function;slot->u1.Function=(ULONGLONG)&ObserveClip;DWORD ignored;VirtualProtect(&slot->u1.Function,8,old,&ignored);Log("MOUSE_CAPTURE_PROBE_INSTALLED");return;}}
}
#include "corpse_menu_recovery_policy.h"
static bool OwnedAutomaticCorpseMenu(void* world,uint32_t player,uint32_t source);
#include "native_inventory_dispatch.h"
#include "corpse_menu_recovery.h"
static bool networkInstalled=false;
static uint8_t* networkCall=nullptr;static uint8_t* networkStub=nullptr;
static void* earlyWorld=nullptr;static uint32_t earlyPlayer=0;
static uint32_t pendingLoot=0;static uint64_t lootReady=0,lastLoot=0;
static uint32_t netSequence=0;static uint64_t netUntil=0,netCalls=0,lastAutoRequest=0;
// Diagnostic only: keep the locally rendered root locomotion state during our
// own ordinary resource pickup. Never modify the replicated component or actor.
// These four sequence IDs were exported with game.guid.hash from actual assets.
static bool OrdinaryLootSequence(uint32_t id){return id==428699201u||id==2227211401u||id==304577033u||id==3963881283u;}
#include "resource_tail_policy.h"
struct ResourceTail {void* world=nullptr;uint32_t player=0,source=0,sequence=0;uint64_t at=0;};static ResourceTail resourceTail;
static bool TrimResourceTail(void* out,void* world,uint32_t player,uint32_t sequence,uint8_t* input,uint8_t* shadow){
 if(!resourceTail.source)return false;
 const auto now=GetTickCount64();auto tail=resourceTail;
 const uint64_t movement=(0xfull<<46)|(1ull<<53)|(1ull<<54)|(1ull<<55);
 const bool manual=(Read<uint64_t>(input,abi::InputData::digitalInput)&~movement)!=0;
 if(world!=tail.world||player!=tail.player||(sequence&&sequence!=tail.sequence)||now-tail.at>2000||manual){resourceTail={};return false;}
 if(GetPrivateProfileIntW(L"Testing",L"TrimCompletedResourceAction",0,ini.c_str())!=1)return false;
 if(!resource_tail::MayCancel(true,true,sequence==tail.sequence&&OrdinaryLootSequence(sequence),Component(world,abi::WorldTransform::hash,tail.source)==nullptr,manual,MenusBlocked(),GameForeground(),directedTestActive,now-tail.at))return false;
 memcpy(shadow,input,abi::Input::size);auto bits=Read<uint64_t>(shadow,abi::InputData::digitalInput)|(1ull<<13);memcpy(shadow+abi::InputData::digitalInput,&bits,8);memcpy(out,&shadow,sizeof shadow);resourceTail={};
 Log("COMPLETED_RESOURCE_CANCEL source=%u sequence=%u age_ms=%llu physics_modified=0",tail.source,sequence,(unsigned long long)(now-tail.at));return true;
}
static void ObserveMovement(void* world,uint32_t player,uint32_t sequence,uint8_t* input){
 static uint64_t at=0;static int64_t xyz[3]={};auto now=GetTickCount64();
 if(now-at<100)return;
 auto pos=Component(world,abi::WorldTransform::hash,player);auto velocity=Component(world,abi::NetworkVelocity::hash,player);
 if(pos){int64_t next[3];memcpy(next,pos,24);double dist=0;for(int i=0;i<3;++i){double d=(double(next[i])-double(xyz[i]))/4294967296.0;dist+=d*d;}
 if(at&&now-lastAutoRequest<2500)Log("PICKUP_MOVEMENT sequence=%u measured_speed=%.3f network_speed=%.3f digital=%llx",sequence,sqrt(dist)*1000.0/double(now-at),velocity?Read<float>(velocity,abi::NetworkVelocity::speed):-1.f,Read<unsigned long long>(input,abi::InputData::digitalInput));memcpy(xyz,next,24);}
 at=now;
}
static void LocalAnimationTest(void* world,uint32_t player){
 if(!GetPrivateProfileIntW(L"Testing",L"LocalAnimationFilter",0,ini.c_str()))return;
 if(!getMutable)return;
 auto actor=Component(world,abi::NetworkActor::hash,player);if(!actor)return;
 auto sequence=Read<uint32_t>(actor,abi::NetworkActor::sequenceRuntimeId+abi::SequenceRuntime::sequenceId);
 auto anim=Component(world,abi::ClientAnimation::hash,player);if(!anim)return;
 static uint32_t root=0;static uint8_t rootMotion=0;static bool active=false;
 if(!sequence&&(!lastAutoRequest||GetTickCount64()-lastAutoRequest>2000)){root=Read<uint32_t>(anim,abi::BaseAnimation::animationStateId);rootMotion=Read<uint8_t>(anim,abi::BaseAnimation::animationRootMotionMask);}
 bool scope=root&&OrdinaryLootSequence(sequence)&&lastAutoRequest&&GetTickCount64()-lastAutoRequest<2000;
 if(scope){alignas(16) uint8_t out[32]={};getMutable(out,world,abi::ClientAnimation::hash,player);auto mutableAnim=Read<uint8_t*>(out);if(!mutableAnim)return;
 auto from=Read<uint32_t>(mutableAnim,abi::BaseAnimation::animationStateId);
 memcpy(mutableAnim+abi::BaseAnimation::animationStateId,&root,4);memcpy(mutableAnim+abi::BaseAnimation::animationRootMotionMask,&rootMotion,1);
 // Replicated animation here is a LOCAL receive-side component; no outgoing
 // network packet or server component is changed. This tests the source that
 // overwrites ClientAnimation later in the frame.
 if(GetPrivateProfileIntW(L"Testing",L"LocalAnimationFilter",0,ini.c_str())==2){alignas(16) uint8_t netOut[32]={};getMutable(netOut,world,abi::NetworkAnimation::hash,player);auto net=Read<uint8_t*>(netOut);if(net){memcpy(net+abi::BaseAnimation::animationStateId,&root,4);memcpy(net+abi::BaseAnimation::animationRootMotionMask,&rootMotion,1);}}

 if(!active)Log("LOCAL_ANIMATION_FILTER sequence=%u from=%u locomotion=%u",sequence,from,root);
 }
 if(active&&!scope)Log("LOCAL_ANIMATION_FILTER_END sequence=%u",sequence);active=scope;
}

static void TraceInventory(void* world,uint32_t player,uint8_t* input){
 static uint64_t until=0,at=0,previous=0,twoAgo=0;static unsigned count=0,alternations=0;
 if(!GetPrivateProfileIntW(L"Testing",L"LogInventory",0,ini.c_str())){until=0;return;}
 auto now=GetTickCount64();if(!until){until=now+90000;previous=twoAgo=0;count=alternations=0;}if(now>until||now-at<100)return;at=now;
 auto setup=Component(world,abi::InventorySetup::hash,player);if(!setup)return;
 uint64_t digest=1469598103934665603ull;std::string slots;unsigned pages=0;
 for(unsigned page=0;page<16;++page){const auto id=Read<uint32_t>(setup,abi::InventorySetup::linksEntities+page*4);if(!id)continue;auto inv=Component(world,abi::Inventory::hash,id);if(!inv)continue;++pages;
  slots+=" p"+std::to_string(id)+"k"+std::to_string(Read<uint8_t>(setup,abi::InventorySetup::linksCategories+page))+":";
  for(unsigned slot=0;slot<8;++slot){auto stack=(uint8_t*)inv+slot*abi::Stack::size;for(unsigned b=0;b<abi::Stack::size;++b){digest^=stack[b];digest*=1099511628211ull;}auto item=Read<uint32_t>(stack,abi::Stack::id);if(item)slots+=std::to_string(slot)+"="+std::to_string(item)+"x"+std::to_string(Read<uint32_t>(stack,abi::Stack::data+abi::ItemData::count))+",";}
 }
 if(digest!=previous||count++%50==0){if(digest!=previous&&digest==twoAgo)++alternations;Log("INVENTORY_TRACE pages=%u hash=%llx alternations=%u native_transfer=%u source=%u target=%u type=%u sort=%u%s",pages,(unsigned long long)digest,alternations,Read<uint32_t>(input,abi::InputData::inventoryTransferAction),Read<uint32_t>(input,abi::InputData::inventoryTransferAction+abi::Transfer::sourceEntityId),Read<uint32_t>(input,abi::InputData::inventoryTransferAction+abi::Transfer::targetEntityId),Read<uint8_t>(input,abi::InputData::inventoryTransferAction+abi::Transfer::type),Read<uint32_t>(input,abi::InputData::sortInventoryAction),slots.c_str());twoAgo=previous;previous=digest;}
}
static void* __fastcall OnNetworkInput(void* out,void* world,uint32_t hash,uint32_t player,uint32_t tick){
 void* result=getRead(out,world,hash,player);auto input=Read<uint8_t*>(out);if(!input)return result;
 ++netCalls;static thread_local uint8_t shadow[abi::Input::size];
 TraceInventory(world,player,input);
 static void* prevNetworkWorld=nullptr;if(world!=prevNetworkWorld){Log("WORLD_CONTEXT network=%p early=%p player=%u early_player=%u tick=%u",world,earlyWorld,player,earlyPlayer,tick);prevNetworkWorld=world;}
 auto clientActor=Component(world,abi::ClientActor::hash,player);auto networkActor=Component(world,abi::NetworkActor::hash,player);
 const auto sequence=networkActor?Read<uint32_t>(networkActor,abi::NetworkActor::sequenceRuntimeId+abi::SequenceRuntime::sequenceId):0;
 const auto interactionHost=networkActor?Read<unsigned long long>(networkActor,abi::NetworkActor::interactionHostId):0;
 // Passive check of the replicated target table. Interaction is reflected
 // TargetType2; each GameObjectId is16bytes(value0,type8), not an offer ID.
 auto selectedTargets=Component(world,abi::NetworkSelectedTargets::hash,player);
 static uint64_t targetTableAt=0;if(GetTickCount64()-targetTableAt>=1000){targetTableAt=GetTickCount64();Log("NETWORK_TARGET_TABLE present=%u interaction=%llu interaction_type=%u item=%llu item_type=%u picking=%llu picking_type=%u",selectedTargets!=nullptr,selectedTargets?Read<unsigned long long>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::value):0,selectedTargets?Read<uint8_t>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::type):0,selectedTargets?Read<unsigned long long>(selectedTargets,48):0,selectedTargets?Read<uint8_t>(selectedTargets,56):0,selectedTargets?Read<unsigned long long>(selectedTargets,160):0,selectedTargets?Read<uint8_t>(selectedTargets,168):0);}
 static unsigned long long previousInteractionHost=~0ull;
 if(interactionHost!=previousInteractionHost){auto query=Component(world,0x2a0c1e34,player);Log("NATIVE_INTERACTION_HOST host=%llu sequence=%u query_value=%u safety_pause=%u",interactionHost,sequence,query?Read<uint32_t>(query):0,automationPaused);previousInteractionHost=interactionHost;}
 const auto clientAbility=clientActor?Read<uint32_t>(clientActor,abi::BaseActor::currentAbilityMask):0;
 const auto networkAbility=networkActor?Read<uint32_t>(networkActor,abi::NetworkActor::currentAbilityMask):0;
 const auto playAbility=clientActor?Read<uint32_t>(clientActor,abi::BaseActor::currentAction+abi::ActionSequence::playbackContext+abi::Playback::abilityMask):0;
 auto netAnim=Component(world,abi::NetworkAnimation::hash,player);auto clientAnim=Component(world,abi::ClientAnimation::hash,player);
 const auto netAnimId=netAnim?Read<uint32_t>(netAnim,abi::BaseAnimation::animationStateId):0;
 const auto clientAnimId=clientAnim?Read<uint32_t>(clientAnim,abi::BaseAnimation::animationStateId):0;
 ObserveMovement(world,player,sequence,input);
 if(GetPrivateProfileIntW(L"Testing",L"LogMotion",0,ini.c_str())){
  static uint64_t motionAt=0;const auto now=GetTickCount64();const float sx=Read<float>(input,abi::InputData::movementStick),sy=Read<float>(input,abi::InputData::movementStick+4);
  if(now-motionAt>=200&&(std::abs(sx)>0.01f||std::abs(sy)>0.01f)){motionAt=now;auto v=Component(world,abi::NetworkVelocity::hash,player);auto locomotion=Component(world,0x90b25f1b,player);auto cam=input+abi::InputData::camera;loot_aim::Quat q{};for(unsigned i=0;i<4;++i)q[i]=Read<float>(cam,abi::ClientCamera::orientation+i*4);auto f=loot_aim::Forward(q);Log("MOTION_NATIVE stick=%.5f,%.5f mode=%u camera_forward=%.5f,%.5f,%.5f velocity=%.5f,%.5f,%.5f speed=%.5f directed=%u sequence=%u locomotion=%u locomotion_present=%u sprint=%u",sx,sy,Read<uint8_t>(input,abi::InputData::movementStickInputMode),f[0],f[1],f[2],v?Read<float>(v,abi::NetworkVelocity::direction):0.f,v?Read<float>(v,abi::NetworkVelocity::direction+4):0.f,v?Read<float>(v,abi::NetworkVelocity::direction+8):0.f,v?Read<float>(v,abi::NetworkVelocity::speed):0.f,directedTestActive,sequence,locomotion?Read<uint8_t>(locomotion,36):255u,locomotion!=nullptr,(Read<uint64_t>(input,abi::InputData::digitalInput)&(1ull<<53))!=0);}
 }
 static uint32_t prevPlay=~0u,prevNetAnim=~0u,prevClientAnim=~0u;
 if(playAbility!=prevPlay||netAnimId!=prevNetAnim||clientAnimId!=prevClientAnim){Log("ANIMATION_STATE sequence=%u playback_ability=%x net_anim=%u client_anim=%u",sequence,playAbility,netAnimId,clientAnimId);prevPlay=playAbility;prevNetAnim=netAnimId;prevClientAnim=clientAnimId;auto sa=Component(world,abi::ServerActor::hash,player);Log("ACTOR_PREDICTION server_actor_present=%u",sa!=nullptr);}
 static uint32_t prevSeq=~0u,prevClient=~0u,prevNetwork=~0u;
 if(sequence!=prevSeq||clientAbility!=prevClient||networkAbility!=prevNetwork){Log("ACTOR_STATE sequence=%u client_present=%u client_ability=%x network_ability=%x",sequence,clientActor!=nullptr,clientAbility,networkAbility);prevSeq=sequence;prevClient=clientAbility;prevNetwork=networkAbility;}

 auto seq=GetPrivateProfileIntW(L"Testing",L"NetworkOnce",0,ini.c_str());
 static uint64_t lastGateLog=0;if(GetTickCount64()-lastGateLog>5000){lastGateLog=GetTickCount64();auto ps=Component(world,abi::PlayerState::hash,player);Log("NET_GATE calls=%llu player=%u world=%p pick=%u unsafe=%u cursor=%d state=%x ui_block=%d cursor_age=%llu",(unsigned long long)netCalls,player,world,nearPick,unsafeOffers,CursorVisible(),ps?Read<uint32_t>(ps,abi::PlayerState::playerStateMask):0,MenusBlocked(),ClientCursorAge());Log("INPUT_GUARD mask=%x digital=%llx transfer_seq=%u source=%u",Read<uint16_t>(input,abi::InputData::mask),Read<unsigned long long>(input,abi::InputData::digitalInput),GetPrivateProfileIntW(L"Testing",L"TransferOnce",0,ini.c_str()),GetPrivateProfileIntW(L"Testing",L"TransferSource",0,ini.c_str()));}

 if(TrimResourceTail(out,world,player,sequence,input,shadow))return result;
 auto state=Component(world,abi::PlayerState::hash,player);const auto flags=state?Read<uint32_t>(state,abi::PlayerState::playerStateMask):0;const bool allowed=!automationPaused&&menuReady&&pickupMenu.enabled&&pickupMenu.automatic&&(flags&1)&&!(flags&6)&&!MenusBlocked()&&!(Read<uint16_t>(input,abi::InputData::mask)&0xdf)&&GameForeground()&&!(Read<uint64_t>(input,abi::InputData::digitalInput)&0x3fffull);
 // One-shot diagnostic: the same normal inventory-transfer action used by Take All.
 static uint32_t transferSequence=0;
 const auto transferSeq=GetPrivateProfileIntW(L"Testing",L"TransferOnce",0,ini.c_str());
 const bool pendingReady=pendingLoot&&GetTickCount64()>=lootReady&&!GetPrivateProfileIntW(L"Testing",L"SkipTransfer",0,ini.c_str());
 const auto source=pendingReady?pendingLoot:(uint32_t)GetPrivateProfileIntW(L"Testing",L"TransferSource",0,ini.c_str());
 const bool transferAllowed=!automationPaused&&menuReady&&pickupMenu.enabled&&pickupMenu.automatic&&(flags&1)&&!(flags&6)&&!MenusBlocked()&&GameForeground()&&!(Read<uint16_t>(input,abi::InputData::mask)&0xd7)&&!(Read<uint64_t>(input,abi::InputData::digitalInput)&0x3fffull);
 if(((transferSeq&&transferSeq!=transferSequence)||pendingReady)&&transferAllowed&&source){
  transferSequence=transferSeq;if(pendingReady)pendingLoot=0;
  auto pos=Component(world,abi::WorldTransform::hash,player);auto srcPos=Component(world,abi::WorldTransform::hash,source);
  auto offer=Component(world,abi::Offer::hash,source);auto inv=Component(world,abi::Inventory::hash,source);
  auto setup=Component(world,abi::InventorySetup::hash,source);
  const auto verb=offer?Read<uint32_t>(offer):0;
  const bool resourceTest=GetPrivateProfileIntW(L"Testing",L"ResourceTransfer",0,ini.c_str())&&(verb==48836331u||verb==1577711826u||verb==2425743549u);
  const bool container=(verb==2589564171u||verb==183940555u)&&inv&&setup&&Read<uint8_t>(setup,abi::InventorySetup::isInitialized);
  if(loot_config::Allowed(pickupMenu,CachedCategory(source))&&pos&&srcPos&&Distance2(pos,srcPos)<=pickupRadius*pickupRadius&&offer&&Read<uint8_t>(offer,abi::Offer::isOffered)&&(container||resourceTest)){
   // Interaction may roll additional loot. Recheck after that roll before Take All.
   bool questBlocked=false;for(auto [item,count]:InventorySnapshot(world,source))if(count&&std::binary_search(questItems.begin(),questItems.end(),item)){questBlocked=true;break;}
   if(questBlocked){Log("NETWORK_TRANSFER_QUEST_BLOCKED source=%u",source);return result;}
   beforePickup=InventorySnapshot(world,player);verifyAt=GetTickCount64()+1000;verifyWorld=world;verifyPlayer=player;
   auto action=input+abi::InputData::inventoryTransferAction;const uint32_t version=tick;
   memset(action,0,abi::Transfer::size);memcpy(action,&version,4);memcpy(action+abi::Transfer::sourceEntityId,&source,4);memcpy(action+abi::Transfer::targetEntityId,&player,4);action[abi::Transfer::type]=2;
   Log("NETWORK_TRANSFER_REQUEST source=%u target=%u version=%u sequence=%u",source,player,version,transferSeq);
  }else Log("NETWORK_TRANSFER_REJECTED_LOCAL source=%u",source);
 }
 if(pendingLoot&&GetTickCount64()>lootReady+1500){Log("AUTO_LOOT_ABORT timeout source=%u",pendingLoot);pendingLoot=0;}
 if((pickupMenu.corpses||pickupMenu.chests)&&allowed&&!unsafeOffers&&nearLoot&&!pendingLoot&&GetTickCount64()-lastScan<300&&GetTickCount64()-lastLoot>1500){
  auto& t=Track(nearLoot);if(t.attempts<3){++t.attempts;t.lastRequest=GetTickCount64();pendingLoot=nearLoot;lastLoot=GetTickCount64();if(GetPrivateProfileIntW(L"Testing",L"SkipTransfer",0,ini.c_str())){beforePickup=InventorySnapshot(world,player);verifyAt=lastLoot+1000;verifyWorld=world;verifyPlayer=player;}lootReady=lastLoot+250;if(GetPrivateProfileIntW(L"Testing",L"LootOpenFirst",1,ini.c_str()))netUntil=lastLoot+100;Log("AUTO_LOOT_BEGIN source=%u attempt=%u open_first=%u",pendingLoot,t.attempts,GetPrivateProfileIntW(L"Testing",L"LootOpenFirst",1,ini.c_str()));}
 }
 if(allowed&&nearPick&&!unsafeOffers&&GetTickCount64()-lastScan<300&&GetTickCount64()-lastAutoRequest>300){
  auto& t=Track(nearPick);if(t.attempts<12){lastAutoRequest=GetTickCount64();++t.attempts;t.lastRequest=lastAutoRequest;netUntil=GetTickCount64()+(GetPrivateProfileIntW(L"Testing",L"ResourceInputBits",0,ini.c_str())?400:100);if(!verifyAt){beforePickup=InventorySnapshot(world,player);verifyAt=GetTickCount64()+1000;verifyWorld=world;verifyPlayer=player;}Log("AUTO_PICK_REQUEST target=%u distance=%.2f radius=%.2f attempt=%u",nearPick,nearPickDistance,pickupRadius,t.attempts);}
 }

 if(seq&&seq!=netSequence&&!CursorVisible()){netSequence=seq;netUntil=GetTickCount64()+100;if(!verifyAt){beforePickup=InventorySnapshot(world,player);verifyAt=GetTickCount64()+1000;verifyWorld=world;verifyPlayer=player;}Log("NETWORK_CONTEXT_BEGIN player=%u sequence=%u calls=%llu original=%llx",player,seq,(unsigned long long)netCalls,Read<unsigned long long>(input,abi::InputData::digitalInput));}
 if(!allowed)netUntil=0;
 if(allowed&&GetTickCount64()<netUntil){memcpy(shadow,input,sizeof shadow);uint64_t bits=Read<uint64_t>(shadow,abi::InputData::digitalInput)|(GetPrivateProfileIntW(L"Testing",L"HoldOnly",0,ini.c_str())?(GetPrivateProfileIntW(L"Testing",L"HoldOnly",0,ini.c_str())==2?0xa0ull:0x80ull):GetPrivateProfileIntW(L"Testing",L"TapOnly",0,ini.c_str())?0x40ull:(netUntil-GetTickCount64()>50?0x20ull:0x40ull));const auto resourceBits=GetPrivateProfileIntW(L"Testing",L"ResourceInputBits",0,ini.c_str());if(resourceBits&&!pendingLoot){bits=Read<uint64_t>(input,abi::InputData::digitalInput)|uint64_t(resourceBits);static uint64_t at=0;if(GetTickCount64()-at>500){at=GetTickCount64();Log("RESOURCE_ALTERNATE_INPUT bits=%x",resourceBits);}}memcpy(shadow+abi::InputData::digitalInput,&bits,8);auto ptr=shadow;memcpy(out,&ptr,sizeof ptr);}
 // Optional independent input mod: add Evade to a private input copy after
 // pickup edits. This avoids competing serializer hooks or corrupting tick ABI.
 auto dodgeModule=GetModuleHandleW(L"DoubleTapDodge.asi");
 using DodgeApply=bool(__cdecl*)(void*,size_t);
 auto dodge=dodgeModule?(DodgeApply)GetProcAddress(dodgeModule,"DoubleTapDodgeApply"):nullptr;
 if(dodge){auto current=Read<uint8_t*>(out);if(current){if(current!=shadow)memcpy(shadow,current,sizeof shadow);if(dodge(shadow,sizeof shadow)){auto ptr=shadow;memcpy(out,&ptr,sizeof ptr);}}}
 // Addressed corpse transfers are independent of the unsafe Interact route.
 // Known non-loot objects cannot be selected by this action. Keep context paused.
 corpse_menu_recovery::Context(world,player,Read<uint64_t>(input,abi::InputData::digitalInput));
 static uint32_t recoverySequence=0;const auto recoveryOnce=GetPrivateProfileIntW(L"Testing",L"RecoverCorpseMenuOnce",0,ini.c_str());
 const auto recoverySource=GetPrivateProfileIntW(L"Testing",L"RecoverCorpseMenuSource",0,ini.c_str());
 if(recoveryOnce&&recoveryOnce!=recoverySequence&&recoverySource&&corpse_menu_recovery::RawSource(world,player)==recoverySource&&CachedCategory(recoverySource)==loot_config::Category::Corpse){recoverySequence=recoveryOnce;corpse_menu_recovery::Requested(world,player,recoverySource);Log("CORPSE_MENU_RECOVERY_ONCE source=%u",recoverySource);}
 const auto directMask=Read<uint16_t>(input,abi::InputData::mask);
 const auto ownedMenuSource=(directMask&8)?corpse_menu_recovery::Source(world,player):0;
 const auto automaticSource=(directMask&8)?ownedMenuSource:nearDirectCorpse;
 static uint32_t directCorpseSequence=0;
 static uint64_t lastDirectCorpse=0;
 const auto directOnce=GetPrivateProfileIntW(L"Testing",L"DirectCorpseOnce",0,ini.c_str());
 const bool directTest=directOnce&&directOnce!=directCorpseSequence;
 const bool directAuto=!directedTestActive&&targetedCorpseAutoLoot&&pickupMenu.automatic&&automaticSource&&GetTickCount64()-lastScan<300&&GetTickCount64()-lastDirectCorpse>=250&&(!Track(automaticSource).lastRequest||GetTickCount64()-Track(automaticSource).lastRequest>=(Track(automaticSource).attempts<3?300u:1500u));
 const bool lootFocus=GameForeground()||GetPrivateProfileIntW(L"Testing",L"AllowBackgroundLoot",0,ini.c_str())==1;
 const bool directReady=automationPaused&&(directTest||directAuto)&&menuReady&&pickupMenu.enabled&&pickupMenu.corpses&&(flags&1)&&!(flags&6)&&!MenusBlocked()&&lootFocus&&corpse_menu_recovery::TransferMask(directMask,!directTest&&ownedMenuSource!=0)&&!(Read<uint64_t>(input,abi::InputData::digitalInput)&0x3fffull);
 if(directReady){
  if(directTest)directCorpseSequence=directOnce;
  const uint32_t corpseSource=directTest?GetPrivateProfileIntW(L"Testing",L"DirectCorpseSource",0,ini.c_str()):automaticSource;
  auto pos=Component(world,abi::WorldTransform::hash,player);auto srcPos=corpseSource?Component(world,abi::WorldTransform::hash,corpseSource):nullptr;
  auto inv=corpseSource?Component(world,abi::Inventory::hash,corpseSource):nullptr;auto setup=corpseSource?Component(world,abi::InventorySetup::hash,corpseSource):nullptr;
  const bool corpse=CachedCategory(corpseSource)==loot_config::Category::Corpse;
  bool questBlocked=false;auto inventory=corpse&&inv&&setup?InventorySnapshot(world,corpseSource):InventoryCounts{};
  for(auto [item,count]:inventory)if(count&&std::binary_search(questItems.begin(),questItems.end(),item))questBlocked=true;
  if(corpseSource&&corpseSource!=player&&corpse&&pos&&srcPos&&Distance2(pos,srcPos)<=pickupRadius*pickupRadius&&inv&&setup&&Read<uint8_t>(setup,abi::InventorySetup::isInitialized)&&!questBlocked&&(directTest||!inventory.empty())){
   // Submit the normal client event on the UI thread. Never overwrite the
   // serializer's transfer action: prediction and server must see one version.
   if(native_inventory::Queue(world,player,corpseSource,!directTest&&ownedMenuSource!=0)){
    if(!verifyAt){beforePickup=InventorySnapshot(world,player);verifyAt=GetTickCount64()+1200;verifyWorld=world;verifyPlayer=player;}
    lastDirectCorpse=GetTickCount64();auto& tracked=Track(corpseSource);tracked.lastRequest=lastDirectCorpse;++tracked.attempts;
    Log("NATIVE_CORPSE_QUEUED source=%u target=%u item_types=%zu menu_recovery=%u",corpseSource,player,inventory.size(),!directTest&&ownedMenuSource!=0);
   }
  }else if(directTest)Log("DIRECT_CORPSE_TEST_BLOCKED source=%u corpse=%u inventory=%u initialized=%u quest=%u",corpseSource,corpse,inv!=nullptr,setup?Read<uint8_t>(setup,abi::InventorySetup::isInitialized):0,questBlocked);
 }
 // Controlled cold-corpse opening, opt-in separately from addressed transfers.
 // The ordinary automatic Interact path stays paused. Aim a copied server-bound
 // camera at one known corpse, settle, then emit one tap. No visible camera edit.
 static uint32_t directedSequence=0,directedSource=0,directedPlayer=0,directedStartTick=0,selectionConfirmations=0,selectionTick=0;
 static void* directedWorld=nullptr;static uint64_t directedStarted=0;
 static bool directedTapped=false,directedProtected=false;static loot_aim::Camera directedCamera;
 const auto directedOnce=GetPrivateProfileIntW(L"Testing",L"DirectedCorpseOnce",0,ini.c_str());
 static uint64_t lastDirected=0;static bool directedDisabled=false;
 static corpse_transfer::VerifiedAck verifiedAcks[256];static size_t verifiedAckNext=0;
 static corpse_transfer::IssuedLoot issuedLoot[64];static unsigned issuedNext=0;
 const uint64_t movementOnlyBits=(0xfull<<46)|(1ull<<53)|(1ull<<54)|(1ull<<55); // Native WASD also emits 2D target-selection flags; Sprint/Walk/Sneak are movement modes.
 const bool directedGate=!directedDisabled&&automationPaused&&menuReady&&pickupMenu.enabled&&pickupMenu.automatic&&(flags&1)&&!(flags&6)&&!MenusBlocked()&&lootFocus&&!(Read<uint16_t>(input,abi::InputData::mask)&0xdf)&&!(Read<uint64_t>(input,abi::InputData::digitalInput)&~movementOnlyBits);
 static uint64_t directedGateAt=0;if(directedOnce&&GetTickCount64()-directedGateAt>5000){directedGateAt=GetTickCount64();Log("DIRECTED_TEST_GATE once=%u consumed=%u foreground=%u menu_block=%u menu_ready=%u enabled=%u corpse=%u pause=%u flags=%x mask=%x",directedOnce,directedSequence,GameForeground(),MenusBlocked(),menuReady,pickupMenu.enabled,pickupMenu.corpses,automationPaused,flags,Read<uint16_t>(input,abi::InputData::mask));}
 const bool directedManual=directedOnce&&directedOnce!=directedSequence&&pickupMenu.corpses;
 const bool resources=GetPrivateProfileIntW(L"Safety",L"DirectedResourceAutoLoot",0,ini.c_str())==1;
 const bool corpseOpening=GetPrivateProfileIntW(L"Safety",L"DirectedCorpseAutoLoot",0,ini.c_str())==1;uint32_t contextSource=corpseOpening?nearColdCorpse:0;
 if(resources&&nearSafeResource&&(!contextSource||corpse_transfer::PreferOpening(Track(nearSafeResource).lastAttempt,Track(nearSafeResource).distance2,Track(contextSource).lastAttempt,Track(contextSource).distance2)))contextSource=nearSafeResource;
 if(protectedOffers&&selectedTargets&&!directedTestActive){
  const auto selected=Read<uint64_t>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::value);
  const auto type=Read<uint8_t>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::type);
  bool eligible=false;
  if(resources&&selected&&selected<=UINT32_MAX&&type==0){
   const auto id=uint32_t(selected);const auto category=CachedCategory(id);
   if(loot_config::ResourceCategory(category)&&loot_config::Allowed(pickupMenu,category)){
    auto& tracked=Track(id);auto offer=Component(world,abi::Offer::hash,id);auto pos=Component(world,abi::WorldTransform::hash,player);auto dst=Component(world,abi::WorldTransform::hash,id);
    bool quest=false;for(auto [item,count]:InventorySnapshot(world,id))if(count&&std::binary_search(questItems.begin(),questItems.end(),item))quest=true;
    eligible=pos&&dst&&Distance2(pos,dst)<=pickupRadius*pickupRadius&&GetTickCount64()>=tracked.poseRetryAt&&corpse_transfer::ResourceEligible(true,true,offer&&Read<uint8_t>(offer,abi::Offer::isOffered),quest,tracked.lastOpen,GetTickCount64(),tracked.openings);
   }
  }
  contextSource=corpse_transfer::PreferConfirmedSource(contextSource,true,selected,type,eligible);
 }
 const bool interactionReady=clientActor&&networkActor&&corpse_transfer::InteractionReady(clientAbility,networkAbility,playAbility,sequence,abi::Ability::CanInteract);
 static uint64_t interactionReadyAt=0;
 if(!interactionReady||interactionHost)interactionReadyAt=0;else if(!interactionReadyAt)interactionReadyAt=GetTickCount64();
 const bool directedAuto=interactionReadyAt&&GetTickCount64()-interactionReadyAt>=corpse_transfer::ReadyStableMs&&contextSource&&!nearDirectCorpse&&!interactionHost&&!coldOpeningBlockers&&GetTickCount64()-lastScan<500&&GetTickCount64()-lastDirected>=corpse_transfer::StartSpacingMs;
 {auto& s=ClientLootRuntimeStatus;++s.revision;s.tick=GetTickCount64();s.world=reinterpret_cast<uintptr_t>(world);s.player=player;s.menuReady=menuReady;s.enabled=pickupMenu.enabled;s.automatic=pickupMenu.automatic;s.corpses=pickupMenu.corpses;s.radiusMm=uint64_t(pickupRadius*1000);s.foreground=GameForeground();s.menuBlocked=MenusBlocked();s.flags=flags;s.mask=directMask;s.digital=Read<uint64_t>(input,abi::InputData::digitalInput);s.directSource=nearDirectCorpse;s.coldSource=nearColdCorpse;s.resourceSource=nearSafeResource;s.blockers=coldOpeningBlockers;s.interactionHost=interactionHost;s.sequence=sequence;s.interactionReady=interactionReady;s.disabled=directedDisabled;s.directedGate=directedGate;s.directedAuto=directedAuto;s.directedActive=directedTestActive;s.directReady=directReady;s.rawMenu=corpse_menu_recovery::RawSource(world,player);s.ownedMenu=ownedMenuSource;s.settings=native_inventory::preferenceState.load();s.nativeInstalled=native_inventory::installed.load();s.paused=automationPaused;s.targeted=targetedCorpseAutoLoot;s.corpseOpening=corpseOpening;s.resourceOpening=resources;s.lastScanAge=GetTickCount64()-lastScan;s.capture=(existingCapture?existingCapture():ClientCursorCaptured());s.intended=directedSource;s.selected=selectedTargets?Read<uint64_t>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::value):0;s.protectedOffers=protectedOffers;s.confirmations=selectionConfirmations;s.elapsed=directedTestActive?GetTickCount64()-directedStarted:0;s.distanceMm=contextSource?uint64_t(sqrt(Track(contextSource).distance2)*1000):0;s.resourceCategory=contextSource?unsigned(CachedCategory(contextSource)):0;++s.revision;}
 static uint64_t coldGateAt=0;if((nearLoot||nearColdCorpse||nearSafeResource)&&GetTickCount64()-coldGateAt>=2000){coldGateAt=GetTickCount64();Log("COLD_CORPSE_GATE nearby=%u eligible=%u resource=%u gate=%u foreground=%u menu=%u host=%llu blockers=%u scan_age=%llu digital=%llx mask=%x ready=%u sequence=%u",nearLoot,nearColdCorpse,nearSafeResource,directedGate,GameForeground(),MenusBlocked(),interactionHost,coldOpeningBlockers,(unsigned long long)(GetTickCount64()-lastScan),(unsigned long long)Read<uint64_t>(input,abi::InputData::digitalInput),Read<uint16_t>(input,abi::InputData::mask),interactionReady,sequence);}
 if(directedGate&&(directedManual||directedAuto)&&!directedTestActive){
  if(directedManual)directedSequence=directedOnce;const uint32_t aimSource=directedManual?GetPrivateProfileIntW(L"Testing",L"DirectedCorpseSource",0,ini.c_str()):contextSource;
  auto pos=Component(world,abi::WorldTransform::hash,player);auto dst=aimSource?Component(world,abi::WorldTransform::hash,aimSource):nullptr;
  const bool known=DirectedLootTarget(world,aimSource,resources);
  bool quest=false;if(known)for(auto [item,count]:InventorySnapshot(world,aimSource))if(count&&std::binary_search(questItems.begin(),questItems.end(),item))quest=true;
  if(known&&pos&&dst&&Distance2(pos,dst)<=pickupRadius*pickupRadius&&!quest&&!coldOpeningBlockers&&!interactionHost){
   loot_aim::Camera native;auto camera=input+abi::InputData::camera;
   for(unsigned i=0;i<3;++i)native.position[i]=double(Read<int64_t>(camera,abi::ClientCamera::position+i*8))/4294967296.0;
   for(unsigned i=0;i<4;++i)native.orientation[i]=Read<float>(camera,abi::ClientCamera::orientation+i*4);
   native.distance=Read<float>(camera,abi::ClientCamera::distance);
   loot_aim::Vec target{};for(unsigned i=0;i<3;++i)target[i]=double(Read<int64_t>(dst,i*8))/4294967296.0;target[1]+=0.25;
   Track(aimSource).lastAttempt=GetTickCount64();
   if(loot_aim::Compose(native,target,directedCamera)){directedSource=aimSource;directedWorld=world;directedPlayer=player;directedStartTick=tick;selectionConfirmations=0;selectionTick=tick;directedProtected=protectedOffers!=0||CachedCategory(aimSource)==loot_config::Category::AnimalProduce;directedStarted=GetTickCount64();lastDirected=directedStarted;directedTapped=false;directedTestActive=true;if(!verifyAt){beforePickup=InventorySnapshot(world,player);verifyAt=directedStarted+1500;verifyWorld=world;verifyPlayer=player;}++ClientLootRuntimeStatus.begins;Log("DIRECTED_CORPSE_TEST_BEGIN source=%u unsafe=%u automatic=%u visible_camera_changed=0 protected_offers=%u",aimSource,unsafeOffers,!directedManual,protectedOffers);}
   else{Track(aimSource).poseRetryAt=GetTickCount64()+100;static uint64_t cameraFailureAt=0;if(GetTickCount64()-cameraFailureAt>=1000){cameraFailureAt=GetTickCount64();auto f=loot_aim::Forward(native.orientation);++ClientLootRuntimeStatus.poseFails;Log("DIRECTED_CORPSE_TEST_BLOCKED source=%u reason=camera_pose camera=%.6f,%.6f,%.6f quaternion=%.6f,%.6f,%.6f,%.6f distance=%.6f target=%.6f,%.6f,%.6f focus_delta=%.6f,%.6f,%.6f",aimSource,native.position[0],native.position[1],native.position[2],native.orientation[0],native.orientation[1],native.orientation[2],native.orientation[3],native.distance,target[0],target[1],target[2],target[0]-native.position[0]-f[0]*native.distance,target[1]-native.position[1]-f[1]*native.distance,target[2]-native.position[2]-f[2]*native.distance);}}
  }else Log("DIRECTED_CORPSE_TEST_BLOCKED source=%u known=%u quest=%u unsafe=%u active_host=%llu",aimSource,known,quest,unsafeOffers,interactionHost);
 }
 if(directedTestActive){
  const auto elapsed=GetTickCount64()-directedStarted;auto pos=Component(world,abi::WorldTransform::hash,player);auto dst=Component(world,abi::WorldTransform::hash,directedSource);
  directedProtected=directedProtected||protectedOffers!=0;
  const uint64_t selectionAge=elapsed;
  if(tick!=selectionTick){selectionTick=tick;if(selectionAge>=100&&selectedTargets&&Read<uint64_t>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::value)==directedSource&&Read<uint8_t>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::type)==0)++selectionConfirmations;else selectionConfirmations=0;}
  if(!directedGate||!DirectedLootTarget(world,directedSource,resources)||world!=directedWorld||player!=directedPlayer||!pos||!dst||Distance2(pos,dst)>pickupRadius*pickupRadius||coldOpeningBlockers){++ClientLootRuntimeStatus.aborts;Log("DIRECTED_CORPSE_TEST_ABORT source=%u elapsed=%llu gate=%u range=%u unsafe=%u blockers=%u",directedSource,(unsigned long long)elapsed,directedGate,pos&&dst&&Distance2(pos,dst)<=pickupRadius*pickupRadius,unsafeOffers,coldOpeningBlockers);directedTestActive=false;}
  else if(interactionHost&&!directedTapped){Log("DIRECTED_CORPSE_TEST_DEFER source=%u previous_host=%llu before_tap=1",directedSource,interactionHost);directedTestActive=false;}
  else if(interactionHost){
   const uint32_t actual=uint32_t(interactionHost);auto actualPos=Component(world,abi::WorldTransform::hash,actual);bool quest=false;
   bool known=interactionHost==actual&&DirectedLootTarget(world,actual,resources);
   if(!known&&resources&&interactionHost==actual){const auto& tracked=Track(actual);const bool farmIdentity=tracked.produceVerb&&GetTickCount64()-tracked.lastSeen<500&&LivingTameAnimal(world,actual);for(const auto& issued:issuedLoot)if(corpse_transfer::IssuedProduceReceipt(issued,reinterpret_cast<uintptr_t>(world),player,actual,GetTickCount64(),farmIdentity)){known=true;break;}}
   if(known)for(auto [item,count]:InventorySnapshot(world,actual))if(count&&std::binary_search(questItems.begin(),questItems.end(),item))quest=true;
   const bool inRange=actualPos?pos&&Distance2(pos,actualPos)<=pickupRadius*pickupRadius:known&&Track(actual).distance2<=pickupRadius*pickupRadius;
   const bool valid=corpse_transfer::AcceptedCorpse(known,inRange,quest);
   ClientLootRuntimeStatus.accepted+=valid;Log("DIRECTED_CORPSE_TEST_RESULT intended=%u actual=%llu match=%u accepted_corpse=%u elapsed_ms=%llu",directedSource,interactionHost,interactionHost==directedSource,valid,(unsigned long long)elapsed);
   const corpse_transfer::VerifiedAck* replay=nullptr;
   if(!valid&&interactionHost==actual)for(const auto& ack:verifiedAcks)if(corpse_transfer::ReplayedAck(ack,reinterpret_cast<uintptr_t>(world),player,actual,sequence,GetTickCount64(),known||!actualPos,quest,!actualPos)){replay=&ack;break;}
   bool issuedGone=false;if(!valid&&interactionHost==actual)for(const auto& issued:issuedLoot)if(corpse_transfer::GoneIssued(issued,reinterpret_cast<uintptr_t>(world),player,actual,GetTickCount64(),!actualPos)){issuedGone=true;break;}
   if(replay||issuedGone){Log("DIRECTED_ACK_REPLAY source=%u previous_verified_host=%u age_ms=%llu gone=%u sequence_changed=%u issued_gone=%u; waiting_for_idle",directedSource,actual,(unsigned long long)(replay?GetTickCount64()-replay->at:0),actualPos==nullptr,replay?sequence!=replay->sequence:false,issuedGone);if(actual!=directedSource){auto& untouched=Track(directedSource);untouched.lastOpen=0;if(untouched.openings)--untouched.openings;}}
   else if(corpse_transfer::DeferMovedReceipt(known,inRange,quest,actualPos!=nullptr)){++ClientLootRuntimeStatus.deferredRangeReceipts;ClientLootRuntimeStatus.lastDeferredHost=actual;ClientLootRuntimeStatus.lastDeferredDistanceMm=pos?uint64_t(sqrt(Distance2(pos,actualPos))*1000):0;Log("DIRECTED_MOVED_RECEIPT source=%u host=%u; waiting_for_idle",directedSource,actual);if(actual!=directedSource){auto& untouched=Track(directedSource);untouched.lastOpen=0;if(untouched.openings)--untouched.openings;}}
   else if(!valid){const auto& latest=verifiedAcks[(verifiedAckNext+255)%256];Log("ACK_REJECTION_DETAIL world=%p player=%u host=%u sequence=%u latest_world=%p latest_player=%u latest_host=%u latest_sequence=%u age_ms=%llu",world,player,actual,sequence,reinterpret_cast<void*>(latest.world),latest.player,latest.host,latest.sequence,(unsigned long long)(GetTickCount64()-latest.at));directedDisabled=true;Log("DIRECTED_CORPSE_SAFETY_STOP reason=unverified_actual_target host=%u known=%u range=%u quest=%u position=%u",actual,known,inRange,quest,actualPos!=nullptr);}
   else{if(CachedCategory(actual)==loot_config::Category::Corpse)corpse_menu_recovery::Requested(world,player,actual);verifiedAckNext=corpse_transfer::RememberAck(verifiedAcks,{reinterpret_cast<uintptr_t>(world),player,actual,sequence,GetTickCount64()})+1;if(loot_config::ResourceCategory(CachedCategory(actual))&&OrdinaryLootSequence(sequence))resourceTail={world,player,actual,sequence,GetTickCount64()};Track(actual).lastOpen=GetTickCount64();if(actual!=directedSource){auto& untouched=Track(directedSource);untouched.lastOpen=0;if(untouched.openings)--untouched.openings;}}
   directedTestActive=false;
  }
  else if(elapsed>=(directedProtected?750u:corpse_transfer::OpeningTimeoutMs)){++ClientLootRuntimeStatus.timeouts;Log("DIRECTED_CORPSE_TEST_TIMEOUT source=%u tapped=%u selected=%llu confirmed=%u protected=%u",directedSource,directedTapped,selectedTargets?Read<unsigned long long>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::value):0,selectionConfirmations,directedProtected);Track(directedSource).poseRetryAt=GetTickCount64()+250;directedTestActive=false;}
  else{
   // Follow the actual current player/camera focus while walking. The private
   // network pose must not freeze at the point where opening began.
   loot_aim::Camera native;auto nativeCamera=input+abi::InputData::camera;loot_aim::Vec target{};
   for(unsigned i=0;i<3;++i){native.position[i]=double(Read<int64_t>(nativeCamera,abi::ClientCamera::position+i*8))/4294967296.0;target[i]=double(Read<int64_t>(dst,i*8))/4294967296.0;}
   for(unsigned i=0;i<4;++i)native.orientation[i]=Read<float>(nativeCamera,abi::ClientCamera::orientation+i*4);
   native.distance=Read<float>(nativeCamera,abi::ClientCamera::distance);target[1]+=0.25;
   if(!loot_aim::Compose(native,target,directedCamera)){++ClientLootRuntimeStatus.aborts;Log("DIRECTED_CORPSE_TEST_ABORT source=%u reason=current_camera_pose",directedSource);directedTestActive=false;return result;}
   const std::array<float,2> movement{Read<float>(input,abi::InputData::movementStick),Read<float>(input,abi::InputData::movementStick+4)};std::array<float,2> preservedMovement{};
   if(Read<uint8_t>(input,abi::InputData::movementStickInputMode)!=0||!loot_aim::PreserveMovement(native,directedCamera,movement,preservedMovement)){++ClientLootRuntimeStatus.aborts;Log("DIRECTED_CORPSE_TEST_ABORT source=%u reason=unsupported_movement",directedSource);++ClientLootRuntimeStatus.movementFails;directedTestActive=false;return result;}
   auto current=Read<uint8_t*>(out);if(current!=shadow)memcpy(shadow,current,sizeof shadow);auto camera=shadow+abi::InputData::camera;
   memcpy(shadow+abi::InputData::movementStick,preservedMovement.data(),sizeof(float)*2);
   for(unsigned i=0;i<3;++i){auto fixed=(int64_t)std::llround(directedCamera.position[i]*4294967296.0);memcpy(camera+abi::ClientCamera::position+i*8,&fixed,8);}
   memcpy(camera+abi::ClientCamera::orientation,directedCamera.orientation.data(),16);
   if(corpse_transfer::CanTap(directedStartTick,tick,elapsed)&&corpse_transfer::ProtectedTapReady(directedProtected,selectedTargets!=nullptr,selectedTargets?Read<uint64_t>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::value):0,selectedTargets?Read<uint8_t>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::type):0,directedSource,selectionConfirmations,selectionAge)&&!directedTapped){auto bits=Read<uint64_t>(shadow,abi::InputData::digitalInput)|0x40ull;memcpy(shadow+abi::InputData::digitalInput,&bits,8);directedTapped=true;issuedLoot[issuedNext++%64]={reinterpret_cast<uintptr_t>(world),player,directedSource,GetTickCount64(),CachedCategory(directedSource)==loot_config::Category::AnimalProduce};if(CachedCategory(directedSource)==loot_config::Category::Corpse)corpse_menu_recovery::Requested(world,player,directedSource);auto& tracked=Track(directedSource);tracked.lastOpen=GetTickCount64();if(tracked.openings<3)++tracked.openings;++ClientLootRuntimeStatus.taps;Log("DIRECTED_CORPSE_TEST_TAP source=%u elapsed=%llu distance=%.2f protected=%u selected=%llu confirmed=%u",directedSource,(unsigned long long)elapsed,sqrt(Distance2(pos,dst)),directedProtected,selectedTargets?Read<unsigned long long>(selectedTargets,abi::NetworkSelectedTargets::targetIds+2*abi::GameObjectId::size+abi::GameObjectId::value):0,selectionConfirmations);}
   auto ptr=shadow;memcpy(out,&ptr,sizeof ptr);
  }
 }
 return result;
}
static void RestoreExperimentalHud(){
 // Remove only this project's exported HUD experiment, preserving native
 // rendering. New game sessions have no such hook and require no changes.
 auto site=Unique("E8 ?? ?? ?? ?? EB 05 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 8D 20 02 00 00");if(!site)return;
 auto call=site+7;auto target=Rel(call,5,1);if(InText(target)||!native_inventory::Readable(target,12)||target[0]!=0x48||target[1]!=0xb8||target[10]!=0xff||target[11]!=0xe0)return;
 HMODULE owner=nullptr;auto fn=Read<uint8_t*>(target,2);if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)fn,&owner))return;
 using Get=void*(__cdecl*)();auto get=(Get)GetProcAddress(owner,"ClientCorpseHudOriginal");if(!get)return;auto native=(uint8_t*)get();
 const uint8_t prologue[]={0x40,0x55,0x56,0x41,0x57,0x48,0x8d,0xac,0x24};if(!InText(native)||memcmp(native,prologue,sizeof prologue))return;
 const auto distance=native-(call+5);if(distance<INT32_MIN||distance>INT32_MAX)return;DWORD old;if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old))return;
 int32_t rel=(int32_t)distance;memcpy(call+1,&rel,4);DWORD ignored;VirtualProtect(call,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),call,5);Log("EXPERIMENTAL_CORPSE_HUD_REMOVED native_rendering_restored=1");
}
static void InstallNetwork(){
 if(networkInstalled){
  // LoadLibrary retains earlier test modules. Returning to one must restore its
  // network reader as well as its scanner; otherwise the old reader uses a
  // different module's stale queue. Reclaim only our recognized call bridge.
  if(networkCall&&networkStub&&networkCall[0]==0xe8&&Rel(networkCall,5,1)!=networkStub){
   auto current=Rel(networkCall,5,1);MEMORY_BASIC_INFORMATION region{};
   const uint8_t prefix[]={0x48,0x83,0xec,0x38,0x44,0x89,0x74,0x24,0x20,0x48,0xb8};
   const uint8_t suffix[]={0xff,0xd0,0x48,0x83,0xc4,0x38,0xc3};
   if(VirtualQuery(current,&region,sizeof region)&&region.State==MEM_COMMIT&&!(region.Protect&(PAGE_NOACCESS|PAGE_GUARD))&&reinterpret_cast<uintptr_t>(current)+26<=reinterpret_cast<uintptr_t>(region.BaseAddress)+region.RegionSize&&!memcmp(current,prefix,sizeof prefix)&&!memcmp(current+19,suffix,sizeof suffix)){
    DWORD old;if(VirtualProtect(networkCall,5,PAGE_EXECUTE_READWRITE,&old)){int32_t rel=static_cast<int32_t>(networkStub-(networkCall+5));memcpy(networkCall+1,&rel,4);DWORD ignored;VirtualProtect(networkCall,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),networkCall,5);Log("NETWORK_SERIALIZATION_REACTIVATED scanner_and_reader_same_module=1");}
   }
  }
  return;
 }
 if(!textStart){auto base=(uint8_t*)GetModuleHandleW(nullptr);auto nt=(IMAGE_NT_HEADERS64*)(base+((IMAGE_DOS_HEADER*)base)->e_lfanew);auto sec=IMAGE_FIRST_SECTION(nt);for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i,++sec)if(!memcmp(sec->Name,".text",5)){textStart=base+sec->VirtualAddress;textSize=sec->Misc.VirtualSize;}}

 RestoreExperimentalHud();InstallUiProbe();InstallClipProbe();native_inventory::Install();corpse_menu_recovery::Install();
 auto p=Unique("48 8D 4D 88 44 8B 05 ?? ?? ?? ?? 44 8B CF 49 8B 57 08 E8 ?? ?? ?? ?? 4C 8B 28 4D 85 ED");
 if(!p||Read<uint32_t>(Rel(p+4,7,3))!=abi::Input::hash){Log("NETWORK_SITE_NOT_SUPPORTED");networkInstalled=true;return;}
 auto call=p+18;auto original=Rel(call,5,1);
 // May replace an earlier version's call thunk while retaining the independently
 // resolved, untouched generic component reader as the original operation.
 if(!InText((uint8_t*)getRead))return;
 auto stub=(uint8_t*)AllocNear(call,64);if(!stub)return;
 // This caller keeps its simulation tick in R14D. Check that register setup
 // before using a thunk to pass it as an extra Win64 argument.
 const uint8_t tickSetup[]={0x8b,0x9d,0x90,0x0e,0,0};
 if(memcmp(p-50,tickSetup,sizeof tickSetup)||memcmp(p-34,"\x45\x8b\xf0",3)){Log("NETWORK_TICK_SETUP_UNSUPPORTED");return;}
 // sub rsp,38; mov [rsp+20],r14d; mov rax,fn; call rax; add rsp,38; ret.
 const uint8_t bridge[]={0x48,0x83,0xec,0x38,0x44,0x89,0x74,0x24,0x20,0x48,0xb8,0,0,0,0,0,0,0,0,0xff,0xd0,0x48,0x83,0xc4,0x38,0xc3};
 memcpy(stub,bridge,sizeof bridge);uint64_t fn=(uint64_t)&OnNetworkInput;memcpy(stub+11,&fn,8);
 intptr_t displacement=stub-(call+5);if(displacement<INT32_MIN||displacement>INT32_MAX)return;
 DWORD old;if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old))return;
 int32_t rel=(int32_t)displacement;memcpy(call+1,&rel,4);DWORD ignored;VirtualProtect(call,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),call,5);networkCall=call;networkStub=stub;networkInstalled=true;netSequence=GetPrivateProfileIntW(L"Testing",L"NetworkOnce",0,ini.c_str());Log("NETWORK_SERIALIZATION_SITE_INSTALLED original_in_text=%d",InText(original));
}
extern "C" __declspec(dllexport) void __cdecl ProbeStep(void* world,uint32_t player,uint8_t* input,GetFn reader,size_t reg,size_t map){static HMODULE dodgeAddon=LoadLibraryW((dir+L"\\DoubleTapDodge.asi").c_str());(void)dodgeAddon;getRead=reader;registryOffset=reg;entityMapOffset=map;earlyWorld=world;earlyPlayer=player;if(!disabled){InstallNetwork();native_inventory::PulsePreferences();if(!getMutable){auto p=Unique("48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 20 57 48 83 EC 30 48 8B E9 44 89 44 24 50 48 8B 8A ?? ?? ?? ?? 48 8B F2 48 81 C1 88 00 00 00 48 8D 54 24 50 41 8B D9 E8 ?? ?? ?? ?? 4C 8B C8");if(p&&Read<uint32_t>(p,31)==registryOffset){getMutable=(GetFn)p;Log("LOCAL_MUTABLE_GETTER_CHECKED");}}LocalAnimationTest(world,player);ProbeSafe(world,player,input);}}
static void* __fastcall OnGetInput(void* out,void* world,uint32_t hash,uint32_t player) {
 void* result=getMutable(out,world,hash,player);
 if(!disabled&&hash==abi::Input::hash){auto input=Read<uint8_t*>(out);if(input)DevelopmentStep(world,player,input);}
 return result;
}
static void* AllocNear(void* center,size_t size){SYSTEM_INFO si;GetSystemInfo(&si);uintptr_t step=si.dwAllocationGranularity,start=(uintptr_t)center&~(step-1);for(uintptr_t d=step;d<0x70000000;d+=step)for(int sign:{-1,1})if(auto p=VirtualAlloc((void*)(start+sign*d),size,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE))return p;return nullptr;}
static bool Install() {
 // Exact instruction forms + referenced component hash distinguish this client
 // update lookup from similarly shaped lookups elsewhere. Resolve everything
 // and validate before changing the sole call operand.
 auto site=Unique("44 8B 05 ?? ?? ?? ?? 48 8D 8C 24 ?? ?? ?? ?? 49 8B 54 24 08 44 8B CE E8 ?? ?? ?? ?? 4C 8B 30 4C 89 74 24 48 4D 85 F6");
 auto read=Unique("48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 48 8D BA ?? ?? ?? ?? 44 89 44 24 40 48 8B F1 48 8D 54 24 40 48 8B 0F 41 8B D9 48 81 C1 88 00 00 00 E8");
 if(!site||!read)return false;
 if(Read<uint32_t>(Rel(site,7,3))!=abi::Input::hash)return false;
 auto call=site+23;auto mut=Rel(call,5,1);
 if(!InText(mut))return false;
 uint8_t* innerCall=nullptr;
 // There are two calls: the first looks up a component type, the second reads
 // the entity's component. Select by verified argument setup, not first E8.
 const uint8_t args[]={0x4c,0x8b,0xc0,0x44,0x8b,0xcb,0x48,0x8b,0xd7,0x48,0x8b,0xce,0xe8};
 for(size_t i=0;i+sizeof args<96;++i)if(!memcmp(read+i,args,sizeof args)){if(innerCall)return false;innerCall=read+i+12;}
 if(!innerCall)return false;
 auto inner=Rel(innerCall,5,1);
 const uint8_t prefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x44,0x89,0x4c,0x24,0x20,0x57,0x48,0x83,0xec,0x20,0x48,0x8d,0xba};
 if(!InText(inner)||memcmp(inner,prefix,sizeof prefix))return false;
 const uint8_t mapArgs[]={0x48,0x8b,0xcf,0x48,0x8d,0x54,0x24,0x48,0x49,0x8b,0xf0,0xe8};
 uint8_t* mapCall=nullptr;
 for(size_t i=0;i+sizeof mapArgs<64;++i)if(!memcmp(inner+i,mapArgs,sizeof mapArgs)){if(mapCall)return false;mapCall=inner+i+11;}
 if(!mapCall)return false;
 auto mapLookup=Rel(mapCall,5,1);
 const uint8_t mapPrefix[]={0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x7c,0x24,0x10,0x83,0x79,0x54,0x00,0x4c,0x8b,0xc9,0x4c,0x8b,0x59,0x10};
 if(!InText(mapLookup)||memcmp(mapLookup,mapPrefix,sizeof mapPrefix))return false;
 registryOffset=Read<uint32_t>(read,18);entityMapOffset=Read<uint32_t>(inner,23);
 // The caller and sibling getter must agree on the component registry member.
 if(memcmp(mut,"\x48\x89\x5c\x24\x08\x48\x89\x6c\x24\x10",10))return false;
 if(registryOffset>0x4000000||entityMapOffset>0x10000)return false;
 auto stub=(uint8_t*)AllocNear(call,64);if(!stub)return false;
 stub[0]=0x48;stub[1]=0xb8;uint64_t fn=(uint64_t)&OnGetInput;memcpy(stub+2,&fn,8);stub[10]=0xff;stub[11]=0xe0;
 intptr_t displacement=stub-(call+5);if(displacement<INT32_MIN||displacement>INT32_MAX)return false;
 getRead=(GetFn)read;getMutable=(GetFn)mut;
 DWORD old;if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old))return false;
 int32_t rel=(int32_t)displacement;memcpy(call+1,&rel,4);DWORD ignored;VirtualProtect(call,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),call,5);
 Log("CLIENT_PROBE_INSTALLED mode=PASSIVE reflection_input_size=%zu registry_member=%zu entity_map_member=%zu",abi::Input::size,registryOffset,entityMapOffset);return true;
}
static DWORD WINAPI Worker(void*) {
 Log("CLIENT_LOOT_REQUESTS_PROBE version=0.4 default=PASSIVE transfer_requests_require_ini_flag=1");
 FILE* q=nullptr;auto quest=dir+L"\\mods\\auto-loot-plus\\quest_items.txt";
 if(!_wfopen_s(&q,quest.c_str(),L"r")&&q){char line[256];while(fgets(line,sizeof line,q))if(line[0]>='0'&&line[0]<='9')questItems.push_back((uint32_t)strtoul(line,nullptr,10));fclose(q);std::sort(questItems.begin(),questItems.end());}
 if(questItems.empty()){Log("MISSING_QUEST_FILTER disabled");return 0;}
 auto base=(uint8_t*)GetModuleHandleW(nullptr);auto dos=(IMAGE_DOS_HEADER*)base;auto nt=(IMAGE_NT_HEADERS64*)(base+dos->e_lfanew);auto sec=IMAGE_FIRST_SECTION(nt);
 for(unsigned i=0;i<nt->FileHeader.NumberOfSections;++i,++sec)if(!memcmp(sec->Name,".text",5)){textStart=base+sec->VirtualAddress;textSize=sec->Misc.VirtualSize;}
 if(!textStart)return 0;
 for(int n=0;n<240;++n){if(Install())return 0;Sleep(250);}
 Log("UNSUPPORTED_SIGNATURE_OR_LAYOUT no hook installed");return 0;
}
BOOL WINAPI DllMain(HMODULE h,DWORD why,void*) {
 if(why==DLL_PROCESS_ATTACH){DisableThreadLibraryCalls(h);wchar_t path[MAX_PATH];GetModuleFileNameW(nullptr,path,MAX_PATH);auto file=wcsrchr(path,L'\\');if(!file||_wcsicmp(file+1,L"enshrouded.exe"))return TRUE;*file=0;dir=path;ini=dir+L"\\ClientLootRequests.ini";wchar_t log[MAX_PATH];GetPrivateProfileStringW(L"Diagnostics",L"LogPath",L"",log,MAX_PATH,ini.c_str());logPath=log;diagnosticVerbose=GetPrivateProfileIntW(L"Diagnostics",L"Verbose",0,ini.c_str())==1;if(logPath.empty())return TRUE;
#ifndef CLIENT_REQUESTS_EXTENSION
CloseHandle(CreateThread(nullptr,0,Worker,nullptr,0,nullptr));
#else
FILE* q=nullptr;auto quest=dir+L"\\mods\\auto-loot-plus\\quest_items.txt";if(!_wfopen_s(&q,quest.c_str(),L"r")&&q){char line[256];while(fgets(line,sizeof line,q))if(line[0]>='0'&&line[0]<='9')questItems.push_back((uint32_t)strtoul(line,nullptr,10));fclose(q);std::sort(questItems.begin(),questItems.end());}if(questItems.empty())disabled=true;
#endif
}return TRUE;
}











