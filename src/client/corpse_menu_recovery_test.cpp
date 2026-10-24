#include "corpse_menu_recovery_policy.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
static void Check(bool value,const char* label){if(!value){std::fprintf(stderr,"FAIL %s\n",label);std::exit(1);}}
int main(){
 using namespace corpse_menu_recovery;
 // Captured dense pile: intended 148699, observed host 148698, input mask 108.
 Check(!TransferMask(0x108,false),"baseline reproduction: looting flag blocks transfer");
 Check(Owned(148698,148698,0x15,1000,0,1050)&&TransferMask(0x108,true),"exact automatic panel can finish its transfer");
 Check(!Owned(148699,148698,0x15,1000,0,1050),"neighbor corpse cannot use another corpse's panel");
 Check(!Owned(148698,148698,0x15,0,0,1050),"manual or unrecorded panel cannot recover");
 Check(!Owned(148698,148698,0x15,1000,1010,1050),"manual input revokes automatic ownership");
 Check(!Owned(148698,148698,0x15,1000,1000,1050),"same tick manual input fails closed");
 Check(!Owned(148698,148698,0x15,1000,0,11001),"ownership expires without perpetual transfer");
 Check(!Owned(148698,148698,0x15,1000,0,999),"clock before request cannot own menu");
 for(unsigned state=0;state<256;++state)if(state!=0x15)Check(!Owned(148698,148698,(uint8_t)state,1000,0,1050),"other UI states remain protected");
 for(unsigned bit:{0u,1u,2u,4u,6u,7u})Check(!TransferMask((uint16_t)(0x108|(1u<<bit)),true),"other input masks remain blocked");
 Check(TransferMask(0x100,false),"normal gameplay transfer remains available");
 Check(!Owned(0,0,0x15,1000,0,1050),"missing source fails closed");
 Lease active{148698,500};
 Check(active.Holds(148698,0x15,90000,90050,500),"continuously open owned menu survives a long focus pause");
 Check(!active.Holds(148699,0x15,90000,90050,500),"lease cannot transfer to adjacent corpse");
 Check(!active.Holds(148698,1,90000,90050,500),"closed menu invalidates lease");
 Check(!active.Holds(148698,0x15,90000,90050,501),"manual takeover invalidates lease");
 Check(!active.Holds(148698,0x15,90000,90251,500),"stale UI frame cannot validate lease");
 Check(!active.Holds(148698,0x15,90000,89999,500),"future UI frame fails closed");
 Check(!Lease{}.Holds(148698,0x15,90000,90050,500),"new manual menu cannot create ownership");
 std::puts("PASS dense corpse menu recovery policy; native tests only");
}
