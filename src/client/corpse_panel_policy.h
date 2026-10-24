#pragma once
#include <cstdint>
namespace corpse_panel_policy {
// The four Interact states occupy bits 4..7. Attacks, dodge and movement do
// not turn an automatic corpse popup into an explicitly requested manual one.
inline bool ManualInteraction(uint64_t digital){return (digital&0xf0ull)!=0;}
inline bool Hide(uint32_t displayed,uint8_t state,uint64_t requestedAt,uint64_t manualAt,uint64_t now,bool foreground,bool enabled){
 return enabled&&foreground&&displayed&&state==0x15&&requestedAt&&requestedAt>manualAt&&now>=requestedAt&&now-requestedAt<=1500;
}
}
