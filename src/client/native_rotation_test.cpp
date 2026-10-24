#include <windows.h>
#include "loot_aim.h"
#include <cstdio>
#include <vector>
#include <cstdlib>
using Rotate=void*(__fastcall*)(float*,const float*,const float*);
int main(int argc,char**argv){
 if(argc!=2)return 2;FILE* f=nullptr;if(fopen_s(&f,argv[1],"rb")||!f)return 2;
 std::vector<unsigned char> code;for(int c;(c=fgetc(f))!=EOF;)code.push_back((unsigned char)c);fclose(f);
 auto p=VirtualAlloc(nullptr,code.size(),MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE);if(!p)return 2;memcpy(p,code.data(),code.size());DWORD old;if(!VirtualProtect(p,code.size(),PAGE_EXECUTE_READ,&old))return 2;FlushInstructionCache(GetCurrentProcess(),p,code.size());auto rotate=(Rotate)p;
 using namespace loot_aim;
 Camera native{{3750.27059841156,847.1249907016754,1433.9846864044666},{0.3592995703f,0.5367674828f,-0.2701040208f,0.7140225768f},1.5876915455f};
 alignas(16)float v[4]={0,0,1,0},out[4]={};rotate(out,native.orientation.data(),v);auto forward=Forward(native.orientation);
 std::printf("native rotation +Z=(%.7f,%.7f,%.7f) camera forward=(%.7f,%.7f,%.7f)\n",out[0],out[1],out[2],forward[0],forward[1],forward[2]);
 unsigned tested=0,failures=0;double worst=0;
 for(int degrees=0;degrees<360;degrees+=5)for(double height:{-0.25,-1.5,-3.0})for(double radius:{0.0,0.01,0.1,3.0}){
  const double yaw=degrees*3.14159265358979323846/180;auto nf=Forward(native.orientation);Vec focus{};for(unsigned i=0;i<3;++i)focus[i]=native.position[i]+nf[i]*native.distance;
  const Vec target{focus[0]+radius*std::sin(yaw),focus[1]+height,focus[2]+radius*std::cos(yaw)};Camera aimed;if(!Compose(native,target,aimed))return 3;
  for(auto stick:{std::array<float,2>{0,0},{0,1},{0,-1},{1,0},{-1,0},{0.70710677f,0.70710677f},{-0.70710677f,0.70710677f},{0.15f,-0.45f}}){
   std::array<float,2> adjusted;if(!PreserveMovement(native,aimed,stick,adjusted))return 4;
   alignas(16)float a[4]={stick[0],0,stick[1],0},b[4]={adjusted[0],0,adjusted[1],0},wa[4]={},wb[4]={};rotate(wa,native.orientation.data(),a);rotate(wb,aimed.orientation.data(),b);
   auto la=std::hypot(wa[0],wa[2]),lb=std::hypot(wb[0],wb[2]);double error=la>1e-8&&lb>1e-8?std::hypot(wa[0]/la-wb[0]/lb,wa[2]/la-wb[2]/lb):std::abs(la-lb);
   if(error>worst)worst=error;if(error>0.00001)++failures;++tested;
  }
 }
 std::printf("Actual extracted game routine: cases=%u failures=%u worst_direction_error=%.9f\n",tested,failures,worst);VirtualFree(p,0,MEM_RELEASE);return failures?1:0;
}
