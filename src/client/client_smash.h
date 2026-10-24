// Client request prototype: ordinary input only, no damage or health writes.
#include "smash_templates.h"
namespace client_smash {
static bool Context(void* world,uint32_t player,uint32_t target){
 auto p=Component(world,abi::WorldTransform::hash,player);auto dst=Component(world,abi::WorldTransform::hash,target);if(!p||!dst||Distance2(p,dst)>4.0)return false;
 auto map=(MapView*)((uint8_t*)world+registryOffset+entityMapOffset);if(map->capacity>1048576||map->count>map->capacity||!map->occupied||!map->keys||!map->values)return false;
 bool found=false,base=false,actor=false,protectedProp=false;
 for(size_t n=0;n<map->capacity;++n){if(!(map->occupied[n/64]&(1ull<<(n%64))))continue;auto id=map->keys[n];if(!id||id==player||!map->values[n])continue;
  auto t=TemplateForRecord(map->values[n]);auto policy=t?FindSmashTemplate(t->a,t->b):nullptr;
  if(id==target)found=policy&&(policy->flags&1);
  auto pos=Component(world,abi::WorldTransform::hash,id);if(!pos&&policy&&(policy->flags&4))pos=Component(world,0x3b8476dd,id);if(!pos)continue;
  auto d2=Distance2(p,pos);if(policy&&(policy->flags&4)&&d2<165.0*165.0)base=true;
  if(d2<36.0&&Component(world,abi::NetworkActor::hash,id))actor=true;
  if(id!=target&&policy&&(policy->flags&2)&&!(policy->flags&1)&&d2<6.25)protectedProp=true;
 }
 Log("CLIENT_SMASH_CONTEXT target=%u eligible=%u near_base=%u near_actor=%u protected_prop=%u range=%.2f",target,found,base,actor,protectedProp,sqrt(Distance2(p,dst)));
 return found&&!base&&!actor&&!protectedProp;
}
static bool Step(void* out,void* world,uint32_t player,uint32_t tick,uint8_t* input,uint8_t* shadow){
 static uint32_t consumed=0,stage=0,source=0,pressTick=0,owner=0;static void* context=nullptr;static uint64_t pressedAt=0,lastCheck=0;
 auto once=GetPrivateProfileIntW(L"Testing",L"SmashTestOnce",0,ini.c_str());
 if(!stage&&(!once||once==consumed))return false;
 auto bits=Read<uint64_t>(input,abi::InputData::digitalInput);
 bool idle=std::abs(Read<float>(input,abi::InputData::movementStick))<0.05f&&std::abs(Read<float>(input,abi::InputData::movementStick+4))<0.05f;
 auto actor=Component(world,abi::NetworkActor::hash,player);
 if(!GameForeground()||MenusBlocked()||!idle||(bits&~(1ull<<53))||!actor){if(stage){stage=0;Log("CLIENT_SMASH_CANCEL reason=user_input_or_menu");}return false;}
 if(stage){
  if(world!=context||player!=owner||GetTickCount64()-pressedAt>500){stage=0;return false;}
  if(stage==2&&tick!=pressTick){stage=0;return false;}
  const bool pressFrame=stage==1&&tick==pressTick;
  memcpy(shadow,input,abi::Input::size);auto pulse=bits|(pressFrame?1ull:0xaull);memcpy(shadow+abi::InputData::digitalInput,&pulse,8);memcpy(out,&shadow,sizeof shadow);
  if(!pressFrame&&stage==1){stage=2;pressTick=tick;Log("CLIENT_SMASH_RELEASE target=%u tick=%u",source,tick);}return true;
 }
 if(GetTickCount64()-lastCheck<1000)return false;lastCheck=GetTickCount64();
 source=GetPrivateProfileIntW(L"Testing",L"SmashTestTarget",0,ini.c_str());if(!source||!(Read<uint32_t>(actor,abi::NetworkActor::currentAbilityMask)&1))return false;
 auto targets=Component(world,abi::NetworkSelectedTargets::hash,player);
 if(!targets||Read<uint64_t>(targets)!=source||Read<uint8_t>(targets,8)!=0){Log("CLIENT_SMASH_WAIT target=%u reason=not_native_combat_selection selected=%llu",source,targets?Read<unsigned long long>(targets):0);return false;}
 if(!Context(world,player,source))return false;
 consumed=once;context=world;owner=player;pressTick=tick;pressedAt=GetTickCount64();stage=1;
 memcpy(shadow,input,abi::Input::size);auto press=bits|1ull;memcpy(shadow+abi::InputData::digitalInput,&press,8);memcpy(out,&shadow,sizeof shadow);
 Log("CLIENT_SMASH_PRESS target=%u tick=%u camera_modified=0 movement_modified=0",source,tick);return true;
}
}
