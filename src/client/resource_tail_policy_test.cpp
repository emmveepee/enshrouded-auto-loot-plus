#include "resource_tail_policy.h"
#include <cassert>
#include <cstdio>
int main(){
 assert(resource_tail::MayCancel(true,true,true,true,false,false,true,false,100));
 assert(!resource_tail::MayCancel(false,true,true,true,false,false,true,false,100));
 assert(!resource_tail::MayCancel(true,false,true,true,false,false,true,false,100));
 assert(!resource_tail::MayCancel(true,true,false,true,false,false,true,false,100));
 assert(!resource_tail::MayCancel(true,true,true,false,false,false,true,false,100));
 assert(!resource_tail::MayCancel(true,true,true,true,true,false,true,false,100));
 assert(!resource_tail::MayCancel(true,true,true,true,false,true,true,false,100));
 assert(!resource_tail::MayCancel(true,true,true,true,false,false,false,false,100));
 assert(!resource_tail::MayCancel(true,true,true,true,false,false,true,true,100));
 assert(!resource_tail::MayCancel(true,true,true,true,false,false,true,false,2001));
 puts("PASS completed-resource cancel scope; policy only, not gameplay proof");
}
