#include "native_transfer_command.h"
#include <cassert>
#include <cstdio>
int main(){
 for(bool backpack:{false,true}){
  auto command=native_transfer::Command(483884,2,backpack);
  assert(command.size()==40&&command[0]==0x2c&&command[36]==2);
  uint32_t source=0,target=0,flag=0;
  std::memcpy(&source,command.data()+24,4);std::memcpy(&target,command.data()+12,4);std::memcpy(&flag,command.data()+4,4);
  assert(source==483884&&target==2&&flag==(backpack?4u:0u)&&command[8]==backpack);
  for(unsigned i=0;i<40;++i)if(i!=0&&i!=4&&i!=8&&!(i>=12&&i<16)&&!(i>=24&&i<28)&&i!=36)assert(command[i]==0);
 }
 auto stack=native_transfer::Command(545684,2,true,545685,7);uint32_t page=0;std::memcpy(&page,stack.data()+28,4);assert(page==545685&&stack[32]==7&&stack[4]==4);
 auto refill=native_transfer::Refill(545684,2);uint32_t flag=0,source=0,target=0;std::memcpy(&flag,refill.data()+4,4);std::memcpy(&source,refill.data()+24,4);std::memcpy(&target,refill.data()+12,4);
 assert(flag==1&&refill[8]==1&&refill[36]==2&&source==545684&&target==2);
 for(unsigned i=16;i<24;++i)assert(refill[i]==0);for(unsigned i=28;i<36;++i)assert(refill[i]==0);
 auto merge=native_transfer::Merge(100,7,200,3,12);uint32_t src=0,dst=0;uint16_t amount=0;std::memcpy(&src,merge.data()+28,4);std::memcpy(&dst,merge.data()+16,4);std::memcpy(&amount,merge.data()+38,2);assert(src==100&&dst==200&&merge[32]==7&&merge[20]==3&&merge[36]==3&&amount==12);
 for(unsigned i=12;i<16;++i)assert(merge[i]==0);for(unsigned i=24;i<28;++i)assert(merge[i]==0);
 puts("Native UI transfer command layout: PASS (actual dispatcher field mapping; not a gameplay test)");
}
