#pragma once
#include <array>
#include <cmath>
namespace loot_aim {
using Vec=std::array<double,3>;
using Quat=std::array<float,4>;
struct Camera {Vec position;Quat orientation;float distance;};
inline Vec Forward(const Quat& q){
 const double x=q[0],y=q[1],z=q[2],w=q[3];
 return {2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)};
}
inline Vec Right(const Quat& q){const double x=q[0],y=q[1],z=q[2],w=q[3];return {1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y)};}
inline bool Compose(const Camera& native,const Vec& target,Camera& aimed){
 double norm=0;for(auto q:native.orientation){if(!std::isfinite(q))return false;norm+=q*q;}
 if(std::abs(norm-1)>0.01||!std::isfinite(native.distance)||native.distance<0.f||native.distance>20.f)return false;
 auto forward=Forward(native.orientation);Vec focus{},direction{};double distance2=0;
 for(unsigned i=0;i<3;++i){
  if(!std::isfinite(target[i])||!std::isfinite(native.position[i])||std::abs(target[i])>10000000||std::abs(native.position[i])>10000000)return false;
  focus[i]=native.position[i]+forward[i]*native.distance;direction[i]=target[i]-focus[i];distance2+=direction[i]*direction[i];
 }
 if(distance2<0.0001||distance2>400)return false;
 // Underfoot loot needs a nearly vertical ray. A vertical camera has a singular
 // horizontal movement basis; rejecting it used to strand an eligible corpse.
 // Offset only the private camera's focus sideways enough to keep that basis
 // invertible. Its ray still hits the EXACT target; player/render pose is untouched.
 const auto horizontal=std::hypot(direction[0],direction[2]);
 const auto minimumHorizontal=std::abs(direction[1])*0.15;
 if(horizontal<minimumHorizontal){
  double x=horizontal>1e-8?direction[0]:forward[0],z=horizontal>1e-8?direction[2]:forward[2];
  auto h=std::hypot(x,z);if(h<1e-8){x=1;z=0;h=1;}
  direction[0]=x/h*minimumHorizontal;direction[2]=z/h*minimumHorizontal;
  focus[0]=target[0]-direction[0];focus[2]=target[2]-direction[2];
  distance2=direction[0]*direction[0]+direction[1]*direction[1]+direction[2]*direction[2];
 }
 const auto length=std::sqrt(distance2);for(auto& v:direction)v/=length;
 const double yaw=std::atan2(direction[0],direction[2]);const double pitch=-std::asin(direction[1]);
 const double sy=std::sin(yaw/2),cy=std::cos(yaw/2),sp=std::sin(pitch/2),cp=std::cos(pitch/2);
 Camera next=native;next.orientation={float(cy*sp),float(sy*cp),float(-sy*sp),float(cy*cp)};
 for(unsigned i=0;i<3;++i)next.position[i]=focus[i]-direction[i]*native.distance;
 aimed=next;return true;
}
// The server interprets movement relative to the submitted camera heading.
// Changing that heading alone steers held movement toward the loot target.
// Re-express the same world-space vector in the new heading; never add motion.
inline bool PreserveMovement(const Camera& native,const Camera& aimed,const std::array<float,2>& input,std::array<float,2>& output){
 for(auto v:input)if(!std::isfinite(v))return false;
 const double magnitude2=double(input[0])*input[0]+double(input[1])*input[1];
 if(magnitude2>1.001)return false;
 if(magnitude2<1e-12){output={0,0};return true;}
 // Actual game routine rotates (stick.x,0,stick.y) by the FULL quaternion,
 // then projects onto XZ and normalizes. Pitch/roll must remain in this matrix;
 // normalizing forward/right separately changes diagonal movement headings.
 auto af=Forward(native.orientation),ar=Right(native.orientation),bf=Forward(aimed.orientation),br=Right(aimed.orientation);
 const double wx=ar[0]*input[0]+af[0]*input[1],wz=ar[2]*input[0]+af[2]*input[1];
 const double determinant=br[0]*bf[2]-bf[0]*br[2];
 if(!std::isfinite(determinant)||std::abs(determinant)<0.001||std::hypot(wx,wz)<0.00001)return false;
 const double x=(bf[2]*wx-bf[0]*wz)/determinant,y=(-br[2]*wx+br[0]*wz)/determinant;
 const double length=std::hypot(x,y);if(!std::isfinite(length)||length<0.00001)return false;
 const double scale=std::sqrt(magnitude2)/length;output={float(x*scale),float(y*scale)};
 return std::abs(double(output[0])*output[0]+double(output[1])*output[1]-magnitude2)<0.00001;
}
}
