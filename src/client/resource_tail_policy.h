#pragma once
#include <cstdint>
namespace resource_tail {
inline bool MayCancel(bool ownResource,bool sameContext,bool sameSequence,bool sourceGone,bool manualAction,bool menu,bool foreground,bool opening,uint64_t age){
 return ownResource&&sameContext&&sameSequence&&sourceGone&&!manualAction&&!menu&&foreground&&!opening&&age<=2000;
}
}
