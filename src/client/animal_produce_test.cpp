#define CLIENT_REQUESTS_EXTENSION
#include "client_requests.cpp"
static bool tame=true,offered=true;static uint32_t hp=10,maxHp=100,verb=2425743549u;
static uint8_t actorData[abi::NetworkActor::size]{},healthData[8]{},offerData[12]{},tameData[1]{};
static void* __fastcall Getter(void* out,void*,uint32_t hash,uint32_t){
 memcpy(healthData,&hp,4);memcpy(healthData+4,&maxHp,4);memcpy(offerData,&verb,4);offerData[8]=offered;
 void* p=hash==abi::NetworkActor::hash?actorData:hash==abi::NetworkHealth::hash?healthData:hash==abi::Offer::hash?offerData:hash==0x1399e0bau&&tame?tameData:nullptr;memcpy(out,&p,8);return out;
}
static void Check(bool value,const char* label){if(!value){printf("FAIL %s\n",label);exit(1);}}
static loot_config::Category Classify(const TemplatePolicy& p){uint64_t descriptor[2]={p.a,p.b};uint8_t record[64]{};void* ptr=descriptor;memcpy(record+32,&ptr,8);return CategoryForRecord(nullptr,77,record);}
int main(){
 getRead=Getter;templateChecked=true;templateMember=32;unsigned count=0;
 for(const auto& p:templatePolicies)if(p.category==loot_config::Category::AnimalProduce){
  ++count;Check(p.produceVerb==2425743549u,"independently exported produce verb");
  tame=offered=true;hp=10;verb=p.produceVerb;Check(Classify(p)==loot_config::Category::AnimalProduce,"ready living tamed farm animal eligible");
  tame=false;Check(Classify(p)==loot_config::Category::Blocked,"untamed animal excluded");tame=true;
  offered=false;Check(Classify(p)==loot_config::Category::Blocked,"no produce offer excluded");offered=true;
  verb=123;Check(Classify(p)==loot_config::Category::Blocked,"pet/feed/other verb excluded");verb=p.produceVerb;
  hp=0;Check(Classify(p)==loot_config::Category::Corpse,"actual dead farm animal still ordinary corpse");hp=10;
 }
 Check(count==8,"all eight actual tamed producer templates");
 auto& target=Track(77);target.category=loot_config::Category::AnimalProduce;target.produceVerb=2425743549u;target.lastSeen=GetTickCount64();
 pickupMenu.enabled=pickupMenu.automatic=pickupMenu.animalProduce=true;
 Check(DirectedLootTarget(nullptr,77,true),"ready produce accepted");verb=123;Check(!DirectedLootTarget(nullptr,77,true),"offer switches to pet before tap: cached category cannot permit it");verb=2425743549u;
 pickupMenu.animalProduce=false;Check(!DirectedLootTarget(nullptr,77,true),"separate produce setting respected");pickupMenu.animalProduce=true;
 Check(!DirectedLootTarget(nullptr,77,false),"resource route disabled respected");
 corpse_transfer::IssuedLoot issued{1,2,77,100,true};
 Check(corpse_transfer::IssuedProduceReceipt(issued,1,2,77,200,true),"post-harvest pet offer receipt is recognized");
 Check(!corpse_transfer::IssuedProduceReceipt(issued,1,2,78,200,true),"different animal cannot match receipt");
 Check(!corpse_transfer::IssuedProduceReceipt(issued,1,2,77,5101,true),"receipt expires");
 Check(!corpse_transfer::IssuedProduceReceipt(issued,1,2,77,200,false),"missing/dead/untamed identity rejected");
 Check(!corpse_transfer::ProtectedTapReady(true,true,88,0,77,2,120),"another selected animal cannot authorize tap");
 printf("PASS production animal-produce classifier/gates/receipt;8 exported farm animals; no gameplay claim\n");
}
