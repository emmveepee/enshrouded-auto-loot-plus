#include <windows.h>
#include <string>
#include <filesystem>
#include <fstream>
static std::wstring fixtureRoot;static DWORD activeUser=0;static bool activeKey=true,steamKey=true;
static LSTATUS WINAPI SettingsRegistry(HKEY,const wchar_t*,const wchar_t* key,DWORD,LPDWORD,void* data,LPDWORD bytes){
 if(!wcscmp(key,L"SteamPath")){if(!steamKey)return ERROR_FILE_NOT_FOUND;auto needed=DWORD((fixtureRoot.size()+1)*2);if(*bytes<needed)return ERROR_MORE_DATA;memcpy(data,fixtureRoot.c_str(),needed);*bytes=needed;return ERROR_SUCCESS;}
 if(!activeKey)return ERROR_FILE_NOT_FOUND;*(DWORD*)data=activeUser;return ERROR_SUCCESS;
}
#define RegGetValueW SettingsRegistry
#define CLIENT_REQUESTS_EXTENSION
#include "client_requests.cpp"
#undef RegGetValueW
static void Check(bool condition,const char* label){if(!condition){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
int main(){
 namespace fs=std::filesystem;auto root=fs::current_path()/L"settings-profile-fixture";Check(root.parent_path()==fs::current_path(),"fixture remains inside build directory");fixtureRoot=root.wstring();
 auto one=root/L"userdata/1/1203620/remote/user_settings.json",two=root/L"userdata/2/1203620/remote/user_settings.json";
 fs::create_directories(one.parent_path());fs::create_directories(two.parent_path());
 {std::ofstream f(one);f<<"{\"prioritizeBackpackForItemPickup\":true}";}
 bool value=false;Check(native_inventory::ReadBackpackPreference(value)&&value,"ActiveUser0 with sole actual settings profile");
 activeKey=false;Check(native_inventory::ReadBackpackPreference(value)&&value,"missing active account with sole profile");activeKey=true;
 {std::ofstream f(two);f<<"{\"prioritizeBackpackForItemPickup\":false}";}
 Check(!native_inventory::ReadBackpackPreference(value),"multiple profiles cannot be guessed");
 activeUser=2;Check(native_inventory::ReadBackpackPreference(value)&&!value,"explicit active account selects its own profile");
 activeUser=9;Check(!native_inventory::ReadBackpackPreference(value),"missing explicit profile cannot fall back to another account");
 activeUser=0;fs::remove(two);{std::ofstream f(one);f<<"{\"other\":true}";}
 Check(!native_inventory::ReadBackpackPreference(value),"missing setting never defaults");
 steamKey=false;Check(!native_inventory::ReadBackpackPreference(value),"unknown Steam root cannot guess a path");
 fs::remove(one);for(const auto& path:{one.parent_path(),one.parent_path().parent_path(),one.parent_path().parent_path().parent_path(),two.parent_path(),two.parent_path().parent_path(),two.parent_path().parent_path().parent_path(),root/L"userdata",root})fs::remove(path);
 std::puts("PASS actual settings reader: offline sole profile, explicit identity, ambiguity and missing data; native test only");
}
