// Executes the production UI pump, not a second hand-written transfer model.
#include <windows.h>
#include <cstdio>
#include <atomic>
#include <cerrno>
static unsigned memoryQueries=0,fileOpens=0;
static bool slowQueries=false,foreground=true;
static SIZE_T WINAPI TestVirtualQuery(LPCVOID p,PMEMORY_BASIC_INFORMATION m,SIZE_T n){++memoryQueries;if(slowQueries)Sleep(2);return ::VirtualQuery(p,m,n);}
static int TestOpen(FILE** f,const wchar_t* path,const wchar_t*){
 ++fileOpens;*f=nullptr;
 if(path&&wcsstr(path,L"user_settings.json"))return ::_wfopen_s(f,L"hitch-settings-fixture.json",L"rb");
 return ENOENT;
}
static LSTATUS WINAPI TestRegistry(HKEY,const wchar_t*,const wchar_t* name,DWORD,LPDWORD,void* data,LPDWORD bytes){
 if(!wcscmp(name,L"SteamPath")){const wchar_t value[]=L"FixtureSteam";if(*bytes<sizeof value)return ERROR_MORE_DATA;memcpy(data,value,sizeof value);*bytes=sizeof value;return ERROR_SUCCESS;}
 if(!wcscmp(name,L"ActiveUser")){*(DWORD*)data=1;return ERROR_SUCCESS;}return ERROR_FILE_NOT_FOUND;
}
static DWORD WINAPI TestWindowPid(HWND,LPDWORD pid){*pid=foreground?GetCurrentProcessId():0;return 1;}
#define VirtualQuery TestVirtualQuery
#define _wfopen_s TestOpen
#define RegGetValueW TestRegistry
#define GetWindowThreadProcessId TestWindowPid
#define CLIENT_REQUESTS_EXTENSION
#ifdef LOOT_HITCH_BASELINE
#include "../analysis/corpse-hitch-2026-10-24/baseline/client_requests.cpp"
#else
#include "client_requests.cpp"
#endif
#undef VirtualQuery
#undef _wfopen_s
#undef RegGetValueW
#undef GetWindowThreadProcessId
#include <iostream>
static std::map<std::pair<uint32_t,uint32_t>,std::vector<uint8_t>> fixture;
static std::vector<std::array<uint8_t,40>> commands;
static bool blocked=false;static void* expectedWorld=nullptr;
static bool __cdecl Menu(){return blocked;}
static bool __cdecl Capture(){return true;}
static void __fastcall Noop(void*){}
static void* __fastcall Unwrap(void* wrapper){return Read<void*>(wrapper);}
static uint32_t __fastcall Player(void* value){return Read<uint32_t>(value);}
static void __fastcall Dispatch(void*,uint8_t* cmd){std::array<uint8_t,40> copy;memcpy(copy.data(),cmd,40);commands.push_back(copy);}
static void* __fastcall Getter(void* out,void* world,uint32_t hash,uint32_t id){
 auto i=fixture.find({hash,id});void* p=world==expectedWorld&&i!=fixture.end()?i->second.data():nullptr;memcpy(out,&p,8);return out;
}
static uint8_t* Add(uint32_t hash,uint32_t id,size_t bytes){auto& v=fixture[{hash,id}];v.assign(bytes,0);return v.data();}
template<class T> static void Put(void* p,size_t offset,T value){memcpy((uint8_t*)p+offset,&value,sizeof value);}
static void Check(bool ok,const char* label){if(!ok){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
static void Item(uint32_t id,uint32_t item,uint32_t count){auto inv=Add(abi::Inventory::hash,id,abi::Stack::size*8);Put(inv,abi::Stack::id,item);Put(inv,abi::Stack::data+abi::ItemData::count,count);}
static std::atomic<unsigned> reads{0};
#ifndef LOOT_HITCH_BASELINE
static bool SlowPreference(bool& backpack){++reads;Sleep(25);backpack=false;return true;}
#endif
int main(){
 FILE* fixtureFile=nullptr;Check(!::_wfopen_s(&fixtureFile,L"hitch-settings-fixture.json",L"wb"),"settings fixture");fputs("{\"prioritizeBackpackForItemPickup\":true}",fixtureFile);fclose(fixtureFile);
 alignas(16) uint8_t world[16]{};expectedWorld=world;void* wrapper=world;
 struct Client{void* wrapper;uint32_t player;}client{&wrapper,1};void* clientPtr=&client;void* ui=&clientPtr;void* app=&ui;
 native_inventory::appUi=0;native_inventory::uiClient=0;native_inventory::clientWorld=0;native_inventory::clientPlayer=offsetof(Client,player);
 native_inventory::ecsWorld=Unwrap;native_inventory::playerId=Player;native_inventory::dispatch=Dispatch;native_inventory::previousLoop=Noop;native_inventory::installed=true;
 getRead=Getter;externalMenus=Menu;existingCapture=Capture;menuReady=true;pickupMenu.enabled=true;pickupMenu.automatic=true;pickupMenu.corpses=true;pickupRadius=5;
 Add(abi::WorldTransform::hash,1,24);Add(abi::WorldTransform::hash,100,24);auto setup=Add(abi::InventorySetup::hash,100,abi::InventorySetup::size);setup[abi::InventorySetup::isInitialized]=1;
 Item(100,1101501148u,2);Item(1,1101501148u,100);auto& tracked=Track(100);tracked.category=loot_config::Category::Corpse;tracked.lastSeen=GetTickCount64();
#ifndef LOOT_HITCH_BASELINE
 native_inventory::preferenceState=3;
#endif
 auto run=[&](){native_inventory::pending={world,1,100,GetTickCount64(),false};native_inventory::UiHook(app);};
 // First job refills; second job uses source-slot ToBackpack. Simulated slow
 // memory-region queries expose blocking deterministically, not as game FPS.
 slowQueries=true;memoryQueries=fileOpens=0;LARGE_INTEGER begin,end,f;QueryPerformanceFrequency(&f);QueryPerformanceCounter(&begin);run();run();QueryPerformanceCounter(&end);
 Check(commands.size()==2,"refill then ordinary transfer preserved");Check(Read<uint32_t>(commands[0].data(),4)==1&&commands[0][8]==1,"native refill command");Check(Read<uint32_t>(commands[1].data(),4)==4&&commands[1][8]==1&&Read<uint32_t>(commands[1].data(),28)==100,"native backpack/source-slot command");
 std::cout<<"two_jobs_ms="<<1000.0*(end.QuadPart-begin.QuadPart)/f.QuadPart<<" memory_region_queries="<<memoryQueries<<" synchronous_file_opens="<<fileOpens<<'\n';
#ifdef LOOT_HITCH_BASELINE
 Check(memoryQueries>=8&&fileOpens>0,"baseline reproduces blocking calls in actual pump");
#else
 Check(memoryQueries==0&&fileOpens==0,"production UI transfer has no memory-region queries/file opens");
 Check(!ShouldLog("NATIVE_CORPSE_QUEUED source=%u")&&!ShouldLog("NATIVE_CORPSE_REFILL_DISPATCHED")&&!ShouldLog("NATIVE_HOTBAR_MERGE")&&!ShouldLog("INVENTORY_GAIN"),"normal per-corpse logging is quiet");
 Check(ShouldLog("NATIVE_CORPSE_DISABLED ui_exception")&&ShouldLog("DIRECTED_CORPSE_SAFETY_STOP"),"errors remain visible");
#endif
 slowQueries=false;
 auto reject=[&](const char* label){auto n=commands.size();run();Check(commands.size()==n,label);};
 client.player=2;reject("different player rejected");client.player=1;
 uint8_t foreignWorld[16]{};wrapper=foreignWorld;reject("different non-null world rejected");wrapper=world;
 void* original=wrapper;wrapper=nullptr;reject("missing world rejected");wrapper=original;
 client.wrapper=nullptr;reject("transition wrapper rejected");client.wrapper=&wrapper;
 blocked=true;reject("menus pause transfer");blocked=false;
 foreground=false;reject("background gameplay rejected");foreground=true;
 auto previousCommands=commands.size();native_inventory::pending={world,1,100,GetTickCount64()-251,false};native_inventory::UiHook(app);Check(commands.size()==previousCommands,"expired pending job rejected");
 native_inventory::pending={world,1,100,GetTickCount64(),true};native_inventory::UiHook(app);Check(commands.size()==previousCommands,"unowned corpse menu rejected");
 auto pos=fixture[{abi::WorldTransform::hash,100}].data();Put(pos,0,int64_t(10)*4294967296ll);reject("range rechecked");Put(pos,0,int64_t(0));
 questItems={1101501148u};reject("quest inventory rejected");questItems.clear();
 Track(100).category=loot_config::Category::Blocked;reject("non-corpse rejected");Track(100).category=loot_config::Category::Corpse;
 // Hotbar routing still uses the actual reflected SlotSelection pages.
 auto selection=Add(abi::SlotSelection::hash,1,abi::SlotSelection::size);Put(selection,abi::SlotSelection::actionbarEntityIds,uint32_t(200));Item(200,1101501148u,100);
 run();Check(commands.back()[36]==3&&Read<uint32_t>(commands.back().data(),16)==200&&Read<uint16_t>(commands.back().data(),38)==2,"existing hotbar stack priority preserved");
#ifndef LOOT_HITCH_BASELINE
 native_inventory::preferenceState=0;reject("unavailable settings never guessed");
 native_inventory::preferenceReader=SlowPreference;native_inventory::PulsePreferences();
 auto deadline=GetTickCount64()+2000;bool backpack=true;while(!native_inventory::BackpackPreference(backpack)&&GetTickCount64()<deadline)Sleep(1);
 Check(reads==1&&!backpack,"slow settings read publishes on background worker");
 native_inventory::preferenceHeartbeat=0;deadline=GetTickCount64()+2000;while(native_inventory::preferenceRunning&&GetTickCount64()<deadline)Sleep(1);Check(!native_inventory::preferenceRunning,"inactive worker stops");
 // Bogus native context is caught by the real UI hook before dispatch.
 auto n=commands.size();native_inventory::pending={world,1,100,GetTickCount64(),false};native_inventory::UiHook((void*)1);Check(!native_inventory::installed&&commands.size()==n,"bad context fails closed under SEH");
#endif
 std::remove("hitch-settings-fixture.json");std::puts("PASS actual production UI pump safety/transfer/hitch regression; native fixture only");
}
