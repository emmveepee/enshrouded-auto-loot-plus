#pragma once
#include "radius_config.h"
#include <string>
#include <cstdio>
#include <cctype>
#include <cstring>

namespace loot_config {
struct Menu {
    bool enabled=true, automatic=true, instant=true, sight=true;
    bool ground=true, stones=true, collectibles=true, plants=false, farm=false;
    bool corpses=false, chests=false, smash=false, buildable=true, backpack=false;
    bool touchZones=false, debug=false;
    bool animalProduce=true;
    double radius=3;
    unsigned stack=0;
};
inline std::string Trim(const std::string& s) {
    const auto a=s.find_first_not_of(" \t\r\n");
    return a==std::string::npos ? "" : s.substr(a,s.find_last_not_of(" \t\r\n")-a+1);
}
// Read the complete menu snapshot. F8 rewrites the file; a partial rewrite must
// never temporarily re-enable a category the player explicitly disabled.
inline bool ReadMenu(FILE* file, Menu& menu) {
    if(!file)return false;
    struct BoolKey {const char* key; bool Menu::*member;};
    const BoolKey keys[]={
        {"enabled",&Menu::enabled},{"autoLoot",&Menu::automatic},
        {"instantLoot",&Menu::instant},{"requireLineOfSight",&Menu::sight},
        {"collectGroundLoot",&Menu::ground},{"collectStones",&Menu::stones},
        {"collectCollectibles",&Menu::collectibles},{"collectPlants",&Menu::plants},
        {"collectFarmPlants",&Menu::farm},{"lootEnemyCorpses",&Menu::corpses},
        {"lootChests",&Menu::chests},{"autoSmash",&Menu::smash},
        {"smashBuildableAwayFromBase",&Menu::buildable},{"cheatBestBackpack",&Menu::backpack},
        {"touchZones",&Menu::touchZones},{"debugLog",&Menu::debug},
    };
    Menu next=menu; unsigned present=0; bool radiusPresent=false;
    char line[1024];
    while(std::fgets(line,sizeof line,file)) {
        std::string s=Trim(line);
        if(s.empty()||s[0]=='#'||s[0]==';')continue;
        const auto eq=s.find('=');if(eq==std::string::npos)continue;
        const auto key=Trim(s.substr(0,eq));
        auto value=Trim(s.substr(eq+1));
        const auto comment=value.find_first_of("#;");
        if(comment!=std::string::npos)value=Trim(value.substr(0,comment));
        if(key=="radiusMeters") {
            if(!ParseRadiusLine(s.c_str(),next.radius))return false;
            radiusPresent=true;continue;
        }
        if(key=="stackSize") {
            char* end=nullptr;errno=0;
            auto v=std::strtoul(value.c_str(),&end,10);
            if(end==value.c_str()||*end||errno==ERANGE||v>65535)return false;
            next.stack=static_cast<unsigned>(v);continue;
        }
        for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);++i)if(key==keys[i].key) {
            for(char& c:value)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            if(value=="true"||value=="1"||value=="yes"||value=="on")next.*(keys[i].member)=true;
            else if(value=="false"||value=="0"||value=="no"||value=="off")next.*(keys[i].member)=false;
            else return false;
            present|=1u<<i;break;
        }
    }
    // All eleven pickup switches and the radius must be present together.
    if(std::ferror(file)||(present&0x7ff)!=0x7ff||!radiusPresent)return false;
    menu=next;return true;
}
enum class Category { Unknown, Ground, Stone, Collectible, Plant, Farm, Corpse, Chest, Blocked, Growing, AnimalProduce };
// Placed companion animals offer petting, not loot. Exclude the template family
// before dynamic corpse promotion, including pets with zero replicated health.
inline bool PetTemplate(const char* name){constexpr char prefix[]="PLACEMENT_HELPER_Animal_Pet_";return name&&!std::strncmp(name,prefix,sizeof(prefix)-1);}
inline bool ResourceCategory(Category c){return c==Category::Ground||c==Category::Stone||c==Category::Collectible||c==Category::Plant||c==Category::Farm||c==Category::AnimalProduce;}
inline bool UnsupportedContextVerb(Category category, unsigned) {
    // Known loot remains loot regardless of the interaction's displayed label.
    // Furniture and other unclassified objects still fail category validation.
    return !ResourceCategory(category)&&category!=Category::Corpse&&category!=Category::Chest;
}
// An excluded interaction protects itself through exact replicated selection
// before any tap. It does not veto unrelated enabled loot merely by proximity.
// This includes furniture, notes and quest objects: none becomes an eligible
// source, and a server-selected excluded object never authorizes a pickup tap.
inline bool RequiresTargetConfirmation(Category category,bool allowed){return !allowed||UnsupportedContextVerb(category,0);}
// Context input does not encode a target. Keep a conservative three-metre
// exclusion area, and also reject excluded objects nearer than intended loot.
// A distant excluded object must not disable every nearby pickup at radius 10.
inline bool ContextBlocked(double nearestExcluded2, double candidate2) {
    return nearestExcluded2 <= (std::max)(9.0, candidate2);
}
inline bool Allowed(const Menu& m, Category category) {
    if(!m.enabled||!m.automatic)return false;
    switch(category) {
        case Category::Ground:return m.ground;
        case Category::Stone:return m.ground&&m.stones;
        case Category::Collectible:return m.collectibles;
        case Category::Plant:return m.plants;
        case Category::Farm:return m.farm;
        case Category::AnimalProduce:return m.animalProduce;
        case Category::Corpse:return m.corpses;
        case Category::Chest:return m.chests;
        default:return false;
    }
}
}
