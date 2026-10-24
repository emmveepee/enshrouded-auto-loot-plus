#include "loot_aim.h"
#include <cstdio>
#include <cstdlib>
#include <limits>
static void Check(bool b,const char* what){if(!b){std::printf("FAIL %s\n",what);std::exit(1);}}
int main(){
 using namespace loot_aim;
 // Independent captured native pose: +Z ray at the native distance reaches
 // the player's world position +1.75m. This establishes game axis convention.
 Camera native{{3750.27059841156,847.1249907016754,1433.9846864044666},{0.3592995703f,0.5367674828f,-0.2701040208f,0.7140225768f},1.5876915455f};
 auto f=Forward(native.orientation);Vec focus{};for(unsigned i=0;i<3;++i)focus[i]=native.position[i]+f[i]*native.distance;
 Check(std::abs(focus[0]-3751.179443358387)<0.0001&&std::abs(focus[1]-845.849975762)<0.0001&&std::abs(focus[2]-1434.2475588686)<0.0001,"captured native pose establishes positive-Z forward");
 const auto original=native;
 for(auto target:{Vec{3753,844.3,1434},Vec{3748,844.3,1434},Vec{3751,844.3,1437},Vec{3751,844.3,1431}}){
  Camera aimed;Check(Compose(native,target,aimed),"compose four target directions");
  auto direction=Forward(aimed.orientation);Vec delta{};double len=0;for(unsigned i=0;i<3;++i){delta[i]=target[i]-aimed.position[i];len+=delta[i]*delta[i];}len=std::sqrt(len);
  for(unsigned i=0;i<3;++i)Check(std::abs(delta[i]/len-direction[i])<0.00001,"camera ray addresses desired point");
  Check(native.position==original.position&&native.orientation==original.orientation,"native camera is not mutated");
 }
 Camera out;Check(!Compose(native,{std::numeric_limits<double>::quiet_NaN(),0,0},out),"invalid target rejected");
 for(auto offset:{0.0,0.01,0.1}){
  Vec target{focus[0]+offset,focus[1]-1.5,focus[2]};Camera aimed;
  Check(Compose(native,target,aimed),"loot directly underneath no longer fails camera pose");
  auto direction=Forward(aimed.orientation);double d2=0;for(unsigned i=0;i<3;++i)d2+=(target[i]-aimed.position[i])*(target[i]-aimed.position[i]);
  for(unsigned i=0;i<3;++i)Check(std::abs((target[i]-aimed.position[i])/std::sqrt(d2)-direction[i])<0.00001,"underfoot private camera still points exactly at loot");
  std::array<float,2> adjusted;Check(PreserveMovement(native,aimed,{0,1},adjusted),"underfoot camera preserves movement without singular basis");
  Check(native.position==original.position&&native.orientation==original.orientation,"underfoot camera never mutates native camera");
 }
 auto clipped=native;clipped.distance=0;Check(Compose(clipped,{native.position[0]+1,native.position[1]-1,native.position[2]},out),"collision-compressed camera distance zero is valid");
 auto invalid=native;invalid.orientation={0,0,0,0};Check(!Compose(invalid,{3753,844,1434},out),"unsupported quaternion rejected");
 Check(!Compose(native,{0,0,0},out),"distant target rejected");
 unsigned movementCases=0;
 for(int degrees=0;degrees<360;degrees+=5){
  const double yaw=degrees*3.14159265358979323846/180;Camera aimed=native;aimed.orientation={0,float(std::sin(yaw/2)),0,float(std::cos(yaw/2))};
  for(auto stick:{std::array<float,2>{0,0},{0,1},{0,-1},{1,0},{-1,0},{0.70710677f,0.70710677f},{-0.70710677f,0.70710677f},{0.15f,-0.45f}}){
   std::array<float,2> adjusted;Check(PreserveMovement(native,aimed,stick,adjusted),"movement remapping supported");
   auto a=Forward(native.orientation),b=Forward(aimed.orientation),ar=Right(native.orientation),br=Right(aimed.orientation);
   double beforeX=ar[0]*stick[0]+a[0]*stick[1],beforeZ=ar[2]*stick[0]+a[2]*stick[1];
   double afterX=br[0]*adjusted[0]+b[0]*adjusted[1],afterZ=br[2]*adjusted[0]+b[2]*adjusted[1];
   auto an=std::hypot(beforeX,beforeZ),bn=std::hypot(afterX,afterZ);if(an>0.00001){beforeX/=an;beforeZ/=an;afterX/=bn;afterZ/=bn;}
   Check(std::hypot(beforeX-afterX,beforeZ-afterZ)<0.000001,"world movement direction and speed preserved at every tested heading");
   if(stick[0]==0&&stick[1]==0)Check(adjusted[0]==0&&adjusted[1]==0,"no input never generates movement");++movementCases;
  }
 }
 std::array<float,2> invalidOutput;Check(!PreserveMovement(native,native,{2,0},invalidOutput),"unsupported movement range rejected rather than clamped");
 std::printf("PASS %u movement-preservation cases; native math only\n",movementCases);
 std::puts("PASS camera-ray calibration and composition tests; native math only, no server targeting claim");
}
