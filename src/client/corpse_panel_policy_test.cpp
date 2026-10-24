#include "corpse_panel_policy.h"
#include <cstdio>
#include <cstdlib>
static void Check(bool ok,const char* text){if(!ok){std::printf("FAIL %s\n",text);std::exit(1);}}
int main(){using corpse_panel_policy::Hide;
 Check(corpse_panel_policy::ManualInteraction(0x40),"recorded ordinary E tap cancels hiding");
 Check(corpse_panel_policy::ManualInteraction(0x80),"held interaction cancels hiding");
 Check(!corpse_panel_policy::ManualInteraction(0xa),"recorded ordinary punch does not reveal auto popup");
 Check(!corpse_panel_policy::ManualInteraction(1ull<<53),"sprint does not reveal auto popup");
 Check(Hide(42,0x15,1000,500,1200,true,true),"recent automatic corpse HUD hidden");
 Check(!Hide(42,0x15,0,500,1200,true,true),"other corpse with no matching receipt visible");
 Check(!Hide(42,0x15,1000,1100,1200,true,true),"manual E after auto request always visible");
 Check(!Hide(42,0x15,1000,1000,1200,true,true),"ambiguous same-time manual input visible");
 Check(!Hide(42,0x16,1000,500,1200,true,true),"full inventory untouched");
 Check(!Hide(42,0x29,1000,500,1200,true,true),"storage inventory untouched");
 Check(!Hide(42,0x15,1000,500,2501,true,true),"expired receipt restores normal HUD");
 Check(!Hide(42,0x15,1300,500,1200,true,true),"future receipt rejected");
 Check(!Hide(42,0x15,1000,500,1200,false,true),"unfocused game untouched");
 Check(!Hide(42,0x15,1000,500,1200,true,false),"disable restores native rendering");
 std::puts("PASS corpse HUD scope policy; native test only");
}
