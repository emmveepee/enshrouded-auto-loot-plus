// Exercise the production classifier with the actual exported template table.
#define CLIENT_REQUESTS_EXTENSION
#include "client_requests.cpp"
static uint8_t actorFixture[abi::NetworkActor::size]{},healthFixture[abi::NetworkHealth::size]{};
static bool hasActor=true,hasHealth=true;
static void* __fastcall TestGetter(void* out,void*,uint32_t hash,uint32_t){void* p=hash==abi::NetworkActor::hash&&hasActor?actorFixture:hash==abi::NetworkHealth::hash&&hasHealth?healthFixture:nullptr;memcpy(out,&p,sizeof p);return out;}
static void Check(bool v,const char* message){if(!v){std::printf("FAIL %s\n",message);std::exit(1);}}
static loot_config::Category Classify(const TemplatePolicy& info,uint32_t hp,uint32_t maximum){
 uint64_t descriptor[2]={info.a,info.b};uint8_t record[64]{};void* ptr=descriptor;memcpy(record+32,&ptr,sizeof ptr);
 memcpy(healthFixture,&hp,4);memcpy(healthFixture+4,&maximum,4);return CategoryForRecord(nullptr,123,record);
}
int main(){
 getRead=TestGetter;templateChecked=true;templateMember=32;
 unsigned pets=0;
 for(const auto& row:templatePolicies)if(loot_config::PetTemplate(row.name)){
  ++pets;for(auto maximum:{0u,100u})for(auto hp:{0u,50u})Check(Classify(row,hp,maximum)==loot_config::Category::Blocked,"every actual pet template excluded for zero/live/dead health");
 }
 Check(pets>=7,"actual dog and cat pet families covered");
 const auto dog=FindTemplatePolicy(0x43be5c332314bc51ull,0x4b8b7504e459eb96ull);Check(dog&&loot_config::PetTemplate(dog->name),"live incident dog independently identified");
 const TemplatePolicy* unknown=nullptr;const TemplatePolicy* wild=nullptr;const TemplatePolicy* growing=nullptr;
 for(const auto& row:templatePolicies){if(!unknown&&row.category==loot_config::Category::Unknown&&!loot_config::PetTemplate(row.name))unknown=&row;if(!wild&&row.category==loot_config::Category::Plant)wild=&row;if(!growing&&row.category==loot_config::Category::Growing)growing=&row;}
 Check(unknown&&wild&&growing,"non-pet fixtures present");
 Check(Classify(*unknown,0,0)==loot_config::Category::Unknown,"invulnerable zero/zero actor is not a corpse");
 Check(Classify(*unknown,10,100)==loot_config::Category::Unknown,"living actor is not a corpse");
 Check(Classify(*unknown,0,100)==loot_config::Category::Corpse,"actual dead damageable actor still becomes corpse");
 hasActor=false;Check(Classify(*wild,0,0)==loot_config::Category::Plant,"wild plant preserved");Check(Classify(*growing,0,0)==loot_config::Category::Growing,"growing crop protection preserved");
 hasActor=true;hasHealth=false;Check(Classify(*unknown,0,100)==loot_config::Category::Unknown,"missing health fails closed");
 std::printf("PASS production classifier; %u exported pet templates blocked; dead/living/zero-health actor and crop regressions\n",pets);
}
