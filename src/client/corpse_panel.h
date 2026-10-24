// Suppress only the HUD popup owned by a recent automatic corpse request.
// Native inventory events and prediction continue through the ordinary UI loop.
#include "corpse_panel_policy.h"
namespace corpse_panel {
using Draw=void(__fastcall*)();static Draw original=nullptr;
static uint8_t* callSite=nullptr;static uint8_t* bridge=nullptr;
static void** global=nullptr;static size_t objectField=0,viewField=0,sourceField=0,stateField=0;
struct Receipt{uint32_t source=0;uint64_t at=0;};
static SRWLOCK lock=SRWLOCK_INIT;static Receipt receipts[32];static unsigned next=0;
static uint64_t manualAt=0;static void* contextWorld=nullptr;static uint32_t contextPlayer=0;
static void Context(void* world,uint32_t player,bool manual){
 AcquireSRWLockExclusive(&lock);
 if(contextWorld!=world||contextPlayer!=player){for(auto& r:receipts)r={};contextWorld=world;contextPlayer=player;}
 if(manual)manualAt=GetTickCount64();ReleaseSRWLockExclusive(&lock);
}
static void Requested(uint32_t id){AcquireSRWLockExclusive(&lock);receipts[next++%32]={id,GetTickCount64()};ReleaseSRWLockExclusive(&lock);}
static void* View(){
 auto g=global&&native_inventory::Readable(global,8)?*global:nullptr;
 if(!native_inventory::Readable(g,objectField+8))return nullptr;
 auto h=Read<void*>(g,objectField);if(!native_inventory::Readable(h,8))return nullptr;
 auto o=Read<void*>(h);if(!native_inventory::Readable(o,viewField+8))return nullptr;
 auto v=Read<void*>(o,viewField);return native_inventory::Readable(v,(std::max)(sourceField+4,stateField+1))?v:nullptr;
}
static void __fastcall Observe(){
 const auto now=GetTickCount64();auto view=View();const auto source=view?Read<uint32_t>(view,sourceField):0;
 const uint8_t state=view?Read<uint8_t>(view,stateField):uint8_t{0};
 uint64_t at=0,manual=0;AcquireSRWLockShared(&lock);manual=manualAt;for(const auto& r:receipts)if(r.source==source&&r.at>at)at=r.at;ReleaseSRWLockShared(&lock);
 static uint64_t checked=0;static bool enabled=true;
 if(!checked||now-checked>=250){checked=now;enabled=GetPrivateProfileIntW(L"Safety",L"HideAutomaticCorpseHud",1,ini.c_str())==1;}
 const bool hide=corpse_panel_policy::Hide(source,state,at,manual,now,GameForeground(),enabled&&menuReady&&pickupMenu.enabled&&pickupMenu.automatic&&pickupMenu.corpses);
 static uint64_t last=0;static uint32_t previous=0;
 if(state==0x15&&source&&(source!=previous||now-last>=500)){last=now;previous=source;Log("CORPSE_HUD_FRAME source=%u owned=%u request_age=%llu manual_age=%llu hidden=%u",source,at!=0,(unsigned long long)(at?now-at:999999),(unsigned long long)(manual?now-manual:999999),hide);}
 if(!hide)original();
}
static void* NativeTarget(uint8_t* target,const char* exportName){
 if(InText(target))return target;
 if(!native_inventory::Readable(target,12)||target[0]!=0x48||target[1]!=0xb8||target[10]!=0xff||target[11]!=0xe0)return nullptr;
 HMODULE owner=nullptr;auto fn=Read<uint8_t*>(target,2);
 if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,(LPCWSTR)fn,&owner))return nullptr;
 using Get=void*(__cdecl*)();auto get=(Get)GetProcAddress(owner,exportName);return get?get():nullptr;
}
static bool Bind(uint8_t* call,uint8_t* stub){
 const auto distance=stub-(call+5);if(distance<INT32_MIN||distance>INT32_MAX)return false;
 DWORD old;if(!VirtualProtect(call,5,PAGE_EXECUTE_READWRITE,&old))return false;int32_t rel=(int32_t)distance;memcpy(call+1,&rel,4);DWORD ignored;VirtualProtect(call,5,old,&ignored);FlushInstructionCache(GetCurrentProcess(),call,5);return true;
}
static void Install(){
 if(original){if(callSite&&Rel(callSite,5,1)!=bridge&&NativeTarget(Rel(callSite,5,1),"ClientCorpseHudOriginal")==reinterpret_cast<void*>(original))Bind(callSite,bridge);return;}
 auto site=Unique("E8 ?? ?? ?? ?? EB 05 E8 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 8D 20 02 00 00");
 auto g=Unique("4C 8B 05 ?? ?? ?? ?? 45 33 FF 41 8B F7 49 8B 80 ?? ?? ?? ?? 48 8B 08");
 auto v=Unique("48 8B 71 ?? 80 BE ?? ?? ?? ?? 15 48 89 B5 ?? ?? ?? ?? 75 ?? 48 8D 86 ?? ?? ?? ?? 44 39 38");
 if(!site||!g||!v){Log("CORPSE_HUD_UNSUPPORTED site=%u global=%u view=%u",site!=nullptr,g!=nullptr,v!=nullptr);return;}
 auto call=site+7;auto target=(uint8_t*)NativeTarget(Rel(call,5,1),"ClientCorpseHudOriginal");
 const uint8_t prologue[]={0x40,0x55,0x56,0x41,0x57,0x48,0x8d,0xac,0x24};
 if(!target||!InText(target)||memcmp(target,prologue,sizeof prologue)||g<target||g>target+128||v<target||v>target+128)return;
 global=(void**)Rel(g,7,3);objectField=Read<uint32_t>(g,16);viewField=v[3];stateField=Read<uint32_t>(v,6);sourceField=Read<uint32_t>(v,23);
 if(objectField>4096||viewField>256||stateField>65536||sourceField>65536)return;
 auto stub=(uint8_t*)AllocNear(call,64);if(!stub)return;stub[0]=0x48;stub[1]=0xb8;auto fn=(uint64_t)&Observe;memcpy(stub+2,&fn,8);stub[10]=0xff;stub[11]=0xe0;
 original=(Draw)target;callSite=call;bridge=stub;if(!Bind(call,stub)){original=nullptr;return;}
 Log("CORPSE_HUD_FILTER_READY object=%zu view=%zu state=%zu source=%zu",objectField,viewField,stateField,sourceField);
 // Undo our passive observers on the unrelated full inventory display modes.
 auto full=Unique("33 C9 E8 ?? ?? ?? ?? EB ?? B1 01 E8 ?? ?? ?? ?? EB ?? 80 FA 2A");
 if(full)for(unsigned offset:{2u,11u}){auto c=full+offset;auto current=Rel(c,5,1);if(!InText(current)){auto native=(uint8_t*)NativeTarget(current,"ClientCorpsePanelOriginal");if(native&&InText(native))Bind(c,native);}}
}
}
extern "C" __declspec(dllexport) void* __cdecl ClientCorpseHudOriginal(){return (void*)corpse_panel::original;}
