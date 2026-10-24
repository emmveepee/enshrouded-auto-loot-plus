#pragma once
#include <cstdint>
namespace corpse_menu_recovery {
// ClientPlayerFlags::IsInLootingMenu is bit 3 in the actual reflection cache.
// This permits only an addressed transfer, never another contextual input.
inline bool Owned(uint32_t source,uint32_t panel,uint8_t state,uint64_t requested,
                  uint64_t manual,uint64_t now){
 return source&&source==panel&&state==0x15&&requested&&now>=requested&&
        now-requested<=10000&&(!manual||manual<requested);
}
inline bool TransferMask(uint16_t mask,bool owned){return !(mask&(owned?0xd7u:0xdfu));}
// Once an actual automatic menu has been observed, retain ownership of that
// continuous menu while unfocused. A newly appearing menu still needs a fresh
// receipt. Callers clear the lease on any UI source/state or world change.
struct Lease {
 uint32_t source=0;uint64_t manual=0;
 bool Holds(uint32_t panel,uint8_t state,uint64_t observed,uint64_t now,uint64_t latestManual)const{
  return source&&source==panel&&state==0x15&&observed&&now>=observed&&
         now-observed<=250&&manual==latestManual;
 }
};
}
