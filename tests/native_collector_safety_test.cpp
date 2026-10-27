// Runs the production patch installer against synthetic executable memory only.
// No game binary, DLL entry point, worker or event processor is executed.
#include "../src/menu/main.cpp"
#include <cassert>
#include <sstream>
void Overlay_Install() {}
uint64_t g_lastCursorRecenter=0;
static void Signature(uint8_t* at,const char* pattern){
 std::istringstream input(pattern);std::string token;
 while(input>>token)*at++=token=="??"?0x11:uint8_t(strtoul(token.c_str(),nullptr,16));
}
static void Scenario(bool corpses,bool complete){
 auto text=(uint8_t*)VirtualAlloc(nullptr,0x6000,MEM_RESERVE|MEM_COMMIT,PAGE_EXECUTE_READWRITE);assert(text);memset(text,0xcc,0x6000);
 g_textStart=text;g_textSize=0x6000;g_gameDir=L".";g_cfg.corpses=corpses;g_cfg.debugLog=false;g_questItems.clear();g_near=nullptr;
 Signature(text+0x100,kSigBox);Signature(text+0x200,kSigRadii);Signature(text+0x300,kSigMaxDist);
 Signature(text+0x600,kSigSight);Signature(text+0x2000,kSigGate);
 if(complete)Signature(text+0x3000,kSigPull);
 const uint8_t tail[]={0xb2,8,0x49,0x8b,0xc8,0xe9,0,0,0,0};memcpy(text+0x201a,tail,sizeof tail);
 float nearDistance=0.5625f;memcpy(text+0x5800,&nearDistance,4);int32_t rel=int32_t((text+0x5800)-(text+0x200+kSigRadiiNear+4));memcpy(text+0x200+kSigRadiiNear,&rel,4);
 // Existing vanilla query and no-PickupItem branch/loot-all sentinels must
 // remain intact whether the corpse checkbox is on or off.
 Signature(text+0x400,"48 89 85 ?? ?? ?? ?? 8B 05 ?? ?? ?? ?? 89 44 24 20 48 89 B5 ?? ?? ?? ?? 48 C7 85 ?? ?? ?? ?? 20 00 00 00 E8");
 Signature(text+0x500,"48 8B 08 48 85 C9 0F 84 ?? ?? ?? ?? 48 8D 95 ?? ?? ?? ?? E8 ?? ?? ?? ?? 84 C0 0F 84 ?? ?? ?? ?? 44 0F 2F F7 0F 86 ?? ?? ?? ?? 48 8B 83");
 std::vector<uint8_t> before(text,text+0x6000);
 assert(Patch()==complete);
 if(complete){
  assert(!memcmp(text+0x400,before.data()+0x400,64));assert(!memcmp(text+0x500,before.data()+0x500,64));
  assert(memcmp(text+0x100,before.data()+0x100,0x80)); // loose-pickup radius still installed
  assert(text[0x2000]==0x48&&text[0x2001]==0xb8); // ordinary auto-loot gate
  VirtualFree(g_near,0,MEM_RELEASE);g_near=nullptr;
 }else assert(!memcmp(text,before.data(),before.size()));
 VirtualFree(text,0,MEM_RELEASE);
}
int main(){Scenario(false,true);Scenario(true,true);Scenario(true,false);puts("PASS actual native patch: vanilla corpse query/skip/loot-all untouched with checkbox on/off; loose-pickup patches retained; missing required signature causes no writes. Native fixture only.");}
