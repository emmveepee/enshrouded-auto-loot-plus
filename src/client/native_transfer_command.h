#pragma once
#include <array>
#include <cstdint>
#include <cstring>
namespace native_transfer {
// Actual UI command 0x2c, verified against the current game's dispatcher.
// The dispatcher creates TransferItemToInventoryEvent and owns action versions.
inline std::array<uint8_t,40> Command(uint32_t source,uint32_t target,bool backpack,uint32_t sourcePage=0,uint8_t sourceSlot=0){
 std::array<uint8_t,40> cmd{};cmd[0]=0x2c;
 if(backpack){uint32_t flag=4;std::memcpy(cmd.data()+4,&flag,4);cmd[8]=1;}
 std::memcpy(cmd.data()+12,&target,4);std::memcpy(cmd.data()+24,&source,4);cmd[36]=2;
 std::memcpy(cmd.data()+28,&sourcePage,4);cmd[32]=sourceSlot;
 return cmd;
}
// Native RefillStacks flag index 1 (mask 0x02 in the generated event). This
// operation merges into existing compatible stacks; it is separate from the
// forced ToBackpack route used for placing the remainder in empty slots.
inline std::array<uint8_t,40> Refill(uint32_t source,uint32_t target){
 auto cmd=Command(source,target,false);uint32_t flag=1;std::memcpy(cmd.data()+4,&flag,4);cmd[8]=1;return cmd;
}
// Ordinary slot-to-slot CustomAmount transfer, as used by the native inventory
// UI. Owners stay zero when explicit source/target slots are supplied.
inline std::array<uint8_t,40> Merge(uint32_t sourcePage,uint8_t sourceSlot,uint32_t targetPage,uint8_t targetSlot,uint16_t amount){
 auto cmd=Command(0,0,false,sourcePage,sourceSlot);std::memcpy(cmd.data()+16,&targetPage,4);cmd[20]=targetSlot;cmd[36]=3;std::memcpy(cmd.data()+38,&amount,2);return cmd;
}
}
