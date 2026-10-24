// Observe the native corpse HUD source without hooking or suppressing drawing.
namespace corpse_menu_recovery {
static void** global=nullptr;static size_t objectField=0,viewField=0,sourceField=0,stateField=0;
struct Receipt{void* world=nullptr;uint32_t player=0,source=0;uint64_t at=0;};
static SRWLOCK guard=SRWLOCK_INIT;static Receipt receipts[64];static unsigned next=0;
static uint64_t manualAt=0;
static Lease lease;
static void* contextWorld=nullptr;static uint32_t contextPlayer=0,observedSource=0;static uint8_t observedState=0;static uint64_t observedAt=0;
using Draw=void(__fastcall*)();static Draw original=nullptr;
static void Context(void* world,uint32_t player,uint64_t digital){AcquireSRWLockExclusive(&guard);if(world!=contextWorld||player!=contextPlayer){contextWorld=world;contextPlayer=player;observedAt=0;lease={};for(auto& r:receipts)r={};}if(digital&0x3fffull){manualAt=GetTickCount64();lease={};}ReleaseSRWLockExclusive(&guard);}
static void Requested(void* world,uint32_t player,uint32_t source){
 if(!source)return;AcquireSRWLockExclusive(&guard);
 unsigned slot=next++%64;for(unsigned i=0;i<64;++i)if(receipts[i].world==world&&receipts[i].player==player&&receipts[i].source==source){slot=i;break;}
 receipts[slot]={world,player,source,GetTickCount64()};ReleaseSRWLockExclusive(&guard);
}
static void ObserveMenu(){
 // This runs inside the native HUD phase. Read the same checked-layout chain
 // the native routine is about to read, under DrawHook's exception guard.
 // VirtualQuery on every link costs milliseconds in this process.
 auto g=global?*global:nullptr;if(!g)return;
 auto handle=Read<void*>(g,objectField);if(!handle)return;
 auto object=Read<void*>(handle);if(!object)return;
 auto view=Read<void*>(object,viewField);if(!view)return;
 auto source=Read<uint32_t>(view,sourceField);auto state=Read<uint8_t>(view,stateField);
 AcquireSRWLockExclusive(&guard);bool changed=!observedAt||source!=observedSource||state!=observedState;if(state!=0x15||source!=lease.source)lease={};observedSource=source;observedState=state;observedAt=GetTickCount64();ReleaseSRWLockExclusive(&guard);
 if(changed)Log("CORPSE_MENU_OBSERVED source=%u state=%u ui_thread=1",source,state);
 // One setup snapshot permits exact-source recovery after a development
 // reload, without enabling per-corpse tracing or inventing old receipts.
 static bool firstMenu=false;if(!firstMenu&&source&&state==0x15){firstMenu=true;Log("CORPSE_MENU_SNAPSHOT_READY source=%u state=%u",source,state);}
}
static void __fastcall DrawHook(){static LootProfile profile{"corpse_menu_observer"};LARGE_INTEGER start;const bool sample=profile.Begin(start);__try{ObserveMenu();}__except(EXCEPTION_EXECUTE_HANDLER){}if(sample)profile.End(start);original();}
static uint32_t RawSource(void* world,uint32_t player){
 AcquireSRWLockShared(&guard);auto source=contextWorld==world&&contextPlayer==player&&observedAt&&GetTickCount64()-observedAt<=250&&observedState==0x15?observedSource:0;ReleaseSRWLockShared(&guard);return source;
}
static uint32_t Source(void* world,uint32_t player){
 const auto now=GetTickCount64();uint64_t at=0;AcquireSRWLockExclusive(&guard);
 const auto source=contextWorld==world&&contextPlayer==player&&observedAt&&now>=observedAt&&now-observedAt<=250&&observedState==0x15?observedSource:0;
 for(const auto& r:receipts)if(r.world==world&&r.player==player&&r.source==source)at=(std::max)(at,r.at);
 const bool owned=source&&(lease.Holds(source,observedState,observedAt,now,manualAt)||Owned(source,source,observedState,at,manualAt,now));
 if(owned)lease={source,manualAt};ReleaseSRWLockExclusive(&guard);return owned?source:0;
}
static void Install(){
 auto site=Unique("E8 ?? ?? ?? ?? EB 05 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 8D 20 02 00 00");
 auto g=Unique("4C 8B 05 ?? ?? ?? ?? 45 33 FF 41 8B F7 49 8B 80 ?? ?? ?? ?? 48 8B 08");
 auto v=Unique("48 8B 71 ?? 80 BE ?? ?? ?? ?? 15 48 89 B5 ?? ?? ?? ?? 75 ?? 48 8D 86 ?? ?? ?? ?? 44 39 38");
 // Both sequences must belong to the same checked native HUD routine.
 const uint8_t prologue[]={0x40,0x55,0x56,0x41,0x57,0x48,0x8d,0xac,0x24};
 if(!site||!g||!v||g<textStart+26||!InText(g-26)||memcmp(g-26,prologue,sizeof prologue)||v<g||v>g+128||Rel(site+7,5,1)!=g-26){Log("CORPSE_MENU_RECOVERY_UNSUPPORTED");return;}
 auto object=Read<uint32_t>(g,16),view=uint32_t(v[3]),state=Read<uint32_t>(v,6),source=Read<uint32_t>(v,23);
 if(object>4096||view>256||state>65536||source>65536)return;
 objectField=object;viewField=view;stateField=state;sourceField=source;global=(void**)Rel(g,7,3);
 auto call=site+7;auto stub=(uint8_t*)AllocNear(call,64);if(!stub)return;stub[0]=0x48;stub[1]=0xb8;auto fn=(uint64_t)&DrawHook;memcpy(stub+2,&fn,8);stub[10]=0xff;stub[11]=0xe0;
 auto distance=stub-(call+5);if(distance<INT32_MIN||distance>INT32_MAX)return;DWORD old;if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old))return;
 original=(Draw)(g-26);int32_t rel=(int32_t)distance;memcpy(call+1,&rel,4);DWORD ignored;VirtualProtect(call,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),call,5);
 Log("CORPSE_MENU_RECOVERY_READY read_only_observer=1 native_drawing_preserved=1 object=%zu view=%zu state=%zu source=%zu",objectField,viewField,stateField,sourceField);
}
}
extern "C" __declspec(dllexport) void* __cdecl ClientCorpseHudOriginal(){return (void*)corpse_menu_recovery::original;}
static bool OwnedAutomaticCorpseMenu(void* world,uint32_t player,uint32_t source){return source&&corpse_menu_recovery::Source(world,player)==source;}
