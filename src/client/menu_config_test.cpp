#include "menu_config.h"
#include "template_policy.h"
#include <string>
#include <cstdio>
#include <cstdlib>
using namespace loot_config;
static unsigned checks=0;
static void Check(bool b,const char* what){++checks;if(!b){std::printf("FAIL %s\n",what);std::exit(1);}}
static std::string Snapshot(const char* changed=nullptr,const char* value="false"){
 std::string s;for(auto k:{"enabled","autoLoot","instantLoot","requireLineOfSight","collectGroundLoot","collectStones","collectCollectibles","collectPlants","collectFarmPlants","lootEnemyCorpses","lootChests"}){s+=k;s+='=';s+=(changed&&std::string(k)==changed)?value:"true";s+='\n';}return s+"radiusMeters=10\nstackSize=0\n";
}
static bool Parse(const std::string& s,Menu& m){FILE* f=nullptr;tmpfile_s(&f);if(!f)return false;fwrite(s.data(),1,s.size(),f);rewind(f);bool ok=ReadMenu(f,m);fclose(f);return ok;}
int main(){
 const auto pile=FindTemplatePolicy(0x4164daf1a56d2777ull,0x23c74927b1b7d490ull);
 Check(pile&&pile->category==Category::Ground,"observed loose-loot pile classified as ground loot");
 Check(!UnsupportedContextVerb(Category::Ground,183940555u),"known loose-loot offer does not block adjacent corpses");
 Check(UnsupportedContextVerb(Category::Unknown,183940555u),"unknown loot interaction still blocks");
 Check(!UnsupportedContextVerb(Category::Ground,3271007524u),"classified loot is not rejected by verb ID");
 Menu disabledGround;disabledGround.ground=false;
 Check(!Allowed(disabledGround,Category::Ground),"ground checkbox still excludes loose-loot pile");
 Menu disabledCorpse;disabledCorpse.corpses=false;
 Check(!Allowed(disabledCorpse,Category::Corpse),"corpse checkbox still excludes corpses");
 Check(!ContextBlocked(4.72*4.72,1.47*1.47),"distant wasp nest does not block nearby berries");
 Check(ContextBlocked(2.9*2.9,1.47*1.47),"nearby excluded object blocks contextual input");
 Check(ContextBlocked(4.72*4.72,6.0*6.0),"excluded object closer than distant candidate blocks");
 Check(ContextBlocked(9.0,1.0),"exclusion boundary included");
 Menu m;Check(Parse(Snapshot(),m),"complete snapshot");Check(m.radius==10,"radius");
 Check(!Allowed(m,Category::Growing)&&!ResourceCategory(Category::Growing),"growing crops never enter automatic resource queue");
 Check(RequiresTargetConfirmation(Category::Chest,false),"disabled tool-heap chest protects itself without globally blocking nearby corpses");
 Check(RequiresTargetConfirmation(Category::Growing,false),"growing crops require confirmed exact other target");
 Check(RequiresTargetConfirmation(Category::Plant,false),"disabled known plants stay protected beside enabled loot");
 Check(!RequiresTargetConfirmation(Category::Corpse,true),"enabled corpse does not block adjacent loot");
 Check(RequiresTargetConfirmation(Category::Unknown,false),"notes and beds require exact enabled-loot selection before tap instead of globally stopping pickup");
 Check(RequiresTargetConfirmation(Category::Blocked,false),"excluded quest target cannot authorize tap but does not veto separately confirmed loot");
 const auto seedling=FindTemplatePolicy(0x42a5ce4e18fabf99ull,0xb889a62a2798729eull);
 Check(seedling&&seedling->category==Category::Growing,"current seedling blocker independently classified protected");
 const Category categories[]={Category::Ground,Category::Stone,Category::Collectible,Category::Plant,Category::Farm,Category::Corpse,Category::Chest};
 const char* keys[]={"collectGroundLoot","collectStones","collectCollectibles","collectPlants","collectFarmPlants","lootEnemyCorpses","lootChests"};
 for(unsigned i=0;i<7;++i){Check(Parse(Snapshot(keys[i]),m),keys[i]);for(unsigned j=0;j<7;++j)Check(Allowed(m,categories[j])==(i!=j&&!(i==0&&j==1)),"category isolation");}
 for(auto master:{"enabled","autoLoot"}){Check(Parse(Snapshot(master),m),master);for(auto c:categories)Check(!Allowed(m,c),"master blocks all");}
 Check(Parse(Snapshot(),m),"restore");Check(!Allowed(m,Category::Unknown)&&!Allowed(m,Category::Blocked),"unknown and quest blocked");
 for(auto invalid:{std::string("enabled=false\nradiusMeters=1\n"),Snapshot("collectPlants","garbage"),Snapshot()+"radiusMeters=nan\n",Snapshot()+"stackSize=-1\n",Snapshot()+"stackSize=3x\n"}){Check(!Parse(invalid,m),"invalid rejected");Check(m.enabled&&m.plants&&m.radius==10,"last good retained");}
 for(auto& row:templatePolicies){Check(FindTemplatePolicy(row.a,row.b)==&row,"GUID lookup");}
 Check(FindTemplatePolicy(0,0)==nullptr,"missing GUID");
 std::printf("PASS %u native configuration/policy/lookup checks; no gameplay claim\n",checks);
}

