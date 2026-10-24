// Included after the checked component accessors and menu guard.
#include <atomic>
#include "native_transfer_command.h"
#include "item_stack_limits.h"
namespace native_inventory {
using Dispatch=void(__fastcall*)(void*,uint8_t*);
using PlayerId=uint32_t(__fastcall*)(void*);
using EcsWorld=void*(__fastcall*)(void*);static EcsWorld ecsWorld=nullptr;
static Dispatch dispatch=nullptr;static PlayerId playerId=nullptr;
using Loop=void(__fastcall*)(void*);static Loop previousLoop=nullptr;
static size_t appUi=0,uiClient=0,clientWorld=0,clientPlayer=0;
struct Pending {void* world=nullptr;uint32_t player=0,source=0;uint64_t at=0;bool ownedMenu=false;};
static SRWLOCK lock=SRWLOCK_INIT;static Pending pending;
static std::atomic<bool> installed{false};
struct Refilled{uint32_t source=0;uint64_t at=0;};static Refilled refilled[128];
static void* refillWorld=nullptr;static uint32_t refillPlayer=0;
static bool NeedsRefill(void* world,uint32_t player,uint32_t source,const InventoryCounts& items){
 if(world!=refillWorld||player!=refillPlayer){for(auto& r:refilled)r={};refillWorld=world;refillPlayer=player;}
 for(const auto& r:refilled)if(r.source==source)return false;
 const auto owned=InventorySnapshot(world,player);bool overlap=false;
 for(auto [item,count]:items)if(count&&owned.count(item)){overlap=true;break;}
 if(!overlap)return false;
 auto oldest=&refilled[0];for(auto& r:refilled)if(r.at<oldest->at)oldest=&r;*oldest={source,GetTickCount64()};return true;
}
static bool BackpackSettingsPath(std::wstring& path){
 // Offline Steam may publish ActiveUser=0. A sole Enshrouded settings profile
 // is unambiguous; multiple profiles must not be guessed by date or ordering.
 wchar_t steam[1024]={};DWORD bytes=sizeof steam;DWORD account=0,n=sizeof account;
 if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,steam,&bytes)!=ERROR_SUCCESS||!steam[0])return false;
 auto base=std::wstring(steam)+L"\\userdata\\";
 if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam\\ActiveProcess",L"ActiveUser",RRF_RT_REG_DWORD,nullptr,&account,&n)==ERROR_SUCCESS&&account){path=base+std::to_wstring(account)+L"\\1203620\\remote\\user_settings.json";return true;}
 WIN32_FIND_DATAW entry{};auto find=FindFirstFileW((base+L"*").c_str(),&entry);if(find==INVALID_HANDLE_VALUE)return false;
 unsigned matches=0;std::wstring selected;
 do{
  const std::wstring name=entry.cFileName;if(!(entry.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)||name.empty()||name.size()>10||!std::all_of(name.begin(),name.end(),[](wchar_t c){return c>=L'0'&&c<=L'9';}))continue;
  auto id=_wcstoui64(name.c_str(),nullptr,10);if(!id||id>UINT32_MAX)continue;
  auto candidate=base+name+L"\\1203620\\remote\\user_settings.json";auto attributes=GetFileAttributesW(candidate.c_str());if(attributes==INVALID_FILE_ATTRIBUTES||(attributes&FILE_ATTRIBUTE_DIRECTORY))continue;
  selected=candidate;if(++matches>1)break;
 }while(FindNextFileW(find,&entry));FindClose(find);
 if(matches!=1)return false;path=selected;return true;
}
static bool ReadBackpackPreference(bool& backpack){
 std::wstring path;if(!BackpackSettingsPath(path))return false;
 FILE* f=nullptr;if(_wfopen_s(&f,path.c_str(),L"rb")||!f)return false;
 std::string data;char chunk[4096];size_t count;while((count=fread(chunk,1,sizeof chunk,f))&&data.size()<2*1024*1024)data.append(chunk,count);fclose(f);
 auto key=data.find("\"prioritizeBackpackForItemPickup\"");if(key==std::string::npos)return false;
 auto colon=data.find(':',key);if(colon==std::string::npos)return false;
 auto value=data.find_first_not_of(" \r\n\t",colon+1);if(value==std::string::npos)return false;
 if(data.compare(value,4,"true")==0){backpack=true;return true;}
 if(data.compare(value,5,"false")==0){backpack=false;return true;}return false;
}
// Registry/file access must never run in a UI transfer. Publish one atomic
// snapshot; unavailable settings remain fail-closed rather than guessed.
static std::atomic<unsigned> preferenceState{0}; // 0 unavailable, 1 false, 3 true
static std::atomic<uint64_t> preferenceHeartbeat{0};
static std::atomic<bool> preferenceRunning{false};
using PreferenceReader=bool(*)(bool&);static PreferenceReader preferenceReader=ReadBackpackPreference;
static DWORD WINAPI PreferenceWorker(void*){
 uint64_t refreshed=0;
 while(GetTickCount64()-preferenceHeartbeat.load(std::memory_order_relaxed)<10000){
  auto now=GetTickCount64();if(!refreshed||now-refreshed>=2000){bool value=false;bool valid=preferenceReader(value);auto next=valid?(value?3u:1u):0u;auto previous=preferenceState.exchange(next,std::memory_order_acq_rel);if(!refreshed||previous!=next){if(valid)Log("CORPSE_SETTINGS_READY backpack=%u background_read=1",value);else Log("CORPSE_SETTINGS_UNAVAILABLE pickup_waits_for_settings=1");}refreshed=now;}
  Sleep(250);
 }
 preferenceRunning.store(false,std::memory_order_release);return 0;
}
static void PulsePreferences(){
 preferenceHeartbeat.store(GetTickCount64(),std::memory_order_relaxed);
 if(preferenceRunning.exchange(true,std::memory_order_acq_rel))return;
 auto worker=CreateThread(nullptr,0,PreferenceWorker,nullptr,0,nullptr);
 if(worker)CloseHandle(worker);else preferenceRunning.store(false,std::memory_order_release);
}
static bool BackpackPreference(bool& backpack){auto state=preferenceState.load(std::memory_order_acquire);backpack=(state&2)!=0;return (state&1)!=0;}
static bool Executable(const void* p){MEMORY_BASIC_INFORMATION m{};return VirtualQuery(p,&m,sizeof m)&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&(m.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));}
static bool Readable(const void* p,size_t size){MEMORY_BASIC_INFORMATION m{};return p&&VirtualQuery(p,&m,sizeof m)&&m.State==MEM_COMMIT&&!(m.Protect&(PAGE_GUARD|PAGE_NOACCESS))&&(uintptr_t)p+size>=(uintptr_t)p&&(uintptr_t)p+size<=(uintptr_t)m.BaseAddress+m.RegionSize;}
static bool MergeHotbar(void* world,uint32_t player,uint32_t corpse,void* client){
 // Actual reflected SlotSelection::actionbarEntityIds[2], independently used
 // by the native ToActionbar/ToBackpack transfer routine. No page-order guess.
 auto selection=Component(world,abi::SlotSelection::hash,player);
 static void* checkedWorld=nullptr;static uint32_t checkedPlayer=0;
 if(world!=checkedWorld||player!=checkedPlayer){checkedWorld=world;checkedPlayer=player;Log("NATIVE_HOTBAR_PAGES selection=%u first=%u second=%u",selection!=nullptr,selection?Read<uint32_t>(selection,abi::SlotSelection::actionbarEntityIds):0,selection?Read<uint32_t>(selection,abi::SlotSelection::actionbarEntityIds+4):0);}
 if(!selection)return false;
 auto setup=Component(world,abi::InventorySetup::hash,corpse);if(!setup)return false;
 struct Attempt{void* world=nullptr;uint32_t source=0,target=0,item=0,from=0,to=0;uint8_t s=0,t=0;uint64_t at=0;};static Attempt attempts[64];static unsigned next=0;
 for(unsigned p=0;p<17;++p){auto page=p==0?corpse:Read<uint32_t>(setup,abi::InventorySetup::linksEntities+(p-1)*4);if(!page)continue;auto inv=Component(world,abi::Inventory::hash,page);if(!inv)continue;
  for(unsigned s=0;s<8;++s){auto stack=(uint8_t*)inv+s*abi::Stack::size;auto item=Read<uint32_t>(stack);auto count=Read<uint32_t>(stack,abi::Stack::data+abi::ItemData::count);auto maximum=MaxStack(item);
   if(!item||!count||!maximum||Read<uint32_t>(stack,abi::Stack::data+abi::ItemData::pide))continue; // Persistent/individualized items are not blindly merged.
   for(unsigned bar=0;bar<2;++bar){auto target=Read<uint32_t>(selection,abi::SlotSelection::actionbarEntityIds+bar*4);if(!target||target==page)continue;auto dst=Component(world,abi::Inventory::hash,target);if(!dst)continue;
    for(unsigned t=0;t<8;++t){auto dest=(uint8_t*)dst+t*abi::Stack::size;auto existing=Read<uint32_t>(dest,abi::Stack::data+abi::ItemData::count);
     if(Read<uint32_t>(dest)!=item||!existing||existing>=maximum||Read<uint32_t>(dest,abi::Stack::data+abi::ItemData::pide))continue;
     const auto now=GetTickCount64();bool repeated=false;for(const auto& a:attempts)if(a.world==world&&a.source==page&&a.target==target&&a.s==s&&a.t==t&&a.item==item&&a.from==count&&a.to==existing&&now-a.at<5000){repeated=true;break;}if(repeated)continue;
     const auto amount=(uint16_t)(std::min)(count,maximum-existing);attempts[next++%64]={world,page,target,item,count,existing,(uint8_t)s,(uint8_t)t,now};
     auto command=native_transfer::Merge(page,(uint8_t)s,target,(uint8_t)t,amount);dispatch(client,command.data());
     Log("NATIVE_HOTBAR_MERGE source=%u source_page=%u source_slot=%u target_page=%u target_slot=%u item=%u amount=%u before=%u maximum=%u",corpse,page,s,target,t,item,amount,existing,maximum);return true;
    }
   }
  }
 }
 return false;
}
static void Pump(void* app){
 Pending job;AcquireSRWLockExclusive(&lock);job=pending;pending={};ReleaseSRWLockExclusive(&lock);
 if(!job.source)return;
 if(job.ownedMenu&&!OwnedAutomaticCorpseMenu(job.world,job.player,job.source))return;
 if(GetTickCount64()-job.at>250||MenusBlocked()||!GameForeground()||!menuReady||!pickupMenu.enabled||!pickupMenu.automatic||!pickupMenu.corpses)return;
 // This is the native UI frame's app, under UiHook's exception guard. Read
 // the checked layout without VirtualQuery per link/per corpse. Identity is
 // still resolved afresh; never use a cached world/player to accept a job.
 if(!app)return;auto ui=Read<void*>(app,appUi);if(!ui)return;auto client=Read<void*>(ui,uiClient);if(!client)return;auto wrapper=Read<void*>(client,clientWorld);
 if(!wrapper||ecsWorld(wrapper)!=job.world||playerId((uint8_t*)client+clientPlayer)!=job.player)return;
 auto a=Component(job.world,abi::WorldTransform::hash,job.player);auto b=Component(job.world,abi::WorldTransform::hash,job.source);
 if(!a||!b||Distance2(a,b)>pickupRadius*pickupRadius||CachedCategory(job.source)!=loot_config::Category::Corpse)return;
 auto items=InventorySnapshot(job.world,job.source);if(items.empty())return;
 for(auto [item,count]:items)if(count&&std::binary_search(questItems.begin(),questItems.end(),item))return;
 bool backpack=false;if(!BackpackPreference(backpack)){Log("NATIVE_CORPSE_BLOCKED reason=settings_unreadable source=%u",job.source);return;}
 if(MergeHotbar(job.world,job.player,job.source,client))return;
 // First ask the game to refill existing stacks throughout the player's
 // inventory, including the hotbar. Do this once per corpse, so a full or
 // incompatible stack cannot stall ordinary pickup of its remaining contents.
 if(NeedsRefill(job.world,job.player,job.source,items)){
  auto command=native_transfer::Refill(job.source,job.player);dispatch(client,command.data());
  Log("NATIVE_CORPSE_REFILL_DISPATCHED source=%u target=%u existing_stack_first=1 serializer_action_modified=0",job.source,job.player);return;
 }
 uint32_t sourcePage=0;uint8_t sourceSlot=0;
 auto setup=Component(job.world,abi::InventorySetup::hash,job.source);
 for(unsigned p=0;p<17&&!sourcePage;++p){auto page=p==0?job.source:(setup?Read<uint32_t>(setup,abi::InventorySetup::linksEntities+(p-1)*4):0);if(!page)continue;
  auto inventory=Component(job.world,abi::Inventory::hash,page);if(!inventory)continue;
  for(unsigned slot=0;slot<8;++slot){auto stack=(uint8_t*)inventory+slot*abi::Stack::size;if(Read<uint32_t>(stack,abi::Stack::id)&&Read<uint32_t>(stack,abi::Stack::data+abi::ItemData::count)){sourcePage=page;sourceSlot=(uint8_t)slot;break;}}
 }
 if(!sourcePage)return;
 auto command=native_transfer::Command(job.source,job.player,backpack,sourcePage,sourceSlot);
 dispatch(client,command.data());
 Log("NATIVE_CORPSE_DISPATCHED source=%u target=%u page=%u slot=%u backpack=%u serializer_action_modified=0",job.source,job.player,sourcePage,sourceSlot,backpack);
}
static void InspectUiContext(void* app){
 // Title screens and transitions legitimately have no client/world. Wait for
 // a valid session; never latch an exception-disable merely because of that.
 static bool checked=false;if(checked||!app)return;
 auto ui=Read<void*>(app,appUi);if(!ui)return;
 auto client=Read<void*>(ui,uiClient);if(!client||!Read<void*>(client,clientWorld))return;
 checked=true;
 bool preference=false;bool settings=BackpackPreference(preference);
 Log("NATIVE_CORPSE_UI_CONTEXT valid=1 world=%p player=%u settings_read=%u backpack=%u thread=%lu",ecsWorld(Read<void*>(client,clientWorld)),playerId((uint8_t*)client+clientPlayer),settings,preference,GetCurrentThreadId());
}
static void __fastcall UiHook(void* app){previousLoop(app);if(installed){__try{InspectUiContext(app);Pump(app);}__except(EXCEPTION_EXECUTE_HANDLER){installed=false;Log("NATIVE_CORPSE_DISABLED ui_exception=%lx",GetExceptionCode());}}}
static void Install(){
 // Resolve all layouts from current instructions, including the actual client
 // handle-to-entity conversion. No historical build RVAs are used.
 auto app=Unique("48 8B 8F ?? ?? ?? ?? 48 85 C9 74 08 48 8B D6 E8 ?? ?? ?? ?? 49 83 C6 08");
 auto ui=Unique("48 8B 83 ?? ?? ?? ?? C6 83 ?? ?? ?? ?? 01 C6 83 ?? ?? ?? ?? 00 48 85 C0 74 13 48 8B 88 ?? ?? ?? ?? 48 85 C9");
 auto body=Unique("48 81 EC D0 01 00 00 0F B6 02 48 8B F2 FF C8 48 8B F9 3D 98 00 00 00");
 auto transfer=Unique("48 8B 8F ?? ?? ?? ?? E8 ?? ?? ?? ?? 44 0F B7 0D ?? ?? ?? ?? 41 B8 28 00 00 00 8B 15 ?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 4C 8B F0");
 auto owner=Unique("48 8D 8F ?? ?? ?? ?? E8 ?? ?? ?? ?? 41 89 46 08 48 8B 4E 10 49 89 4E 10");
 auto loopCall=Unique("48 8B CE E8 ?? ?? ?? ?? 48 8B 86 ?? ?? ?? ?? 48 85 C0 74 54 48 8B 80 ?? ?? ?? ??");
 if(!app||!ui||!body||!transfer||!owner||!loopCall){Log("NATIVE_CORPSE_UNSUPPORTED signature_missing=1 app=%u ui=%u dispatch=%u transfer=%u owner=%u loop=%u",app!=nullptr,ui!=nullptr,body!=nullptr,transfer!=nullptr,owner!=nullptr,loopCall!=nullptr);return;}
 appUi=Read<uint32_t>(app,3);uiClient=Read<uint32_t>(ui,3);clientWorld=Read<uint32_t>(transfer,3);clientPlayer=Read<uint32_t>(owner,3);
 playerId=(PlayerId)Rel(owner+7,5,1);ecsWorld=(EcsWorld)Rel(transfer+7,5,1);dispatch=(Dispatch)(body-13);
 const uint8_t unwrap[]={0x48,0x8b,0x41,0x08,0xc3};if(!InText((uint8_t*)ecsWorld)||memcmp((void*)ecsWorld,unwrap,sizeof unwrap))return;
 // The entry may already contain Quick Deposit's detour; calling through it
 // preserves that mod. Never overwrite its prologue or bypass its hook.
 const uint8_t original[]={0x4c,0x8b,0xdc,0x55,0x56,0x57,0x49,0x8d,0xab,0x18,0xff,0xff,0xff};
 auto entry=(uint8_t*)dispatch;bool validEntry=!memcmp(entry,original,sizeof original)||entry[0]==0xe9||(entry[0]==0x48&&entry[1]==0xb8&&entry[10]==0xff&&entry[11]==0xe0)||(entry[0]==0xff&&entry[1]==0x25);
 if(!validEntry||!InText((uint8_t*)playerId)||appUi>0x100000||uiClient>0x10000||clientWorld>0x1000000||clientPlayer>0x1000000)return;
 auto call=loopCall+3;auto previous=Rel(call,5,1);if(!Executable(previous))return;
 auto stub=(uint8_t*)AllocNear(call,64);if(!stub)return;stub[0]=0x48;stub[1]=0xb8;uint64_t fn=(uint64_t)&UiHook;memcpy(stub+2,&fn,8);stub[10]=0xff;stub[11]=0xe0;
 auto distance=stub-(call+5);if(distance<INT32_MIN||distance>INT32_MAX)return;
 DWORD protection;if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&protection))return;
 previousLoop=(Loop)previous;int32_t relative=(int32_t)distance;memcpy(call+1,&relative,4);DWORD ignored;VirtualProtect(call,5,protection,&ignored);FlushInstructionCache(GetCurrentProcess(),call,5);
 installed=true;Log("NATIVE_CORPSE_UI_READY app_field=%zu ui_field=%zu world_field=%zu player_field=%zu",appUi,uiClient,clientWorld,clientPlayer);
}
static bool Queue(void* world,uint32_t player,uint32_t source,bool ownedMenu=false){
 if(!installed)return false;AcquireSRWLockExclusive(&lock);bool free=!pending.source;if(free)pending={world,player,source,GetTickCount64(),ownedMenu};ReleaseSRWLockExclusive(&lock);return free;
}
}
