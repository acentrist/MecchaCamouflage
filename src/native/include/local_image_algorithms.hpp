#pragma once
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
namespace mc::image {
struct RgbImage { int width{0}; int height{0}; std::vector<std::uint8_t> rgb{}; bool valid() const { return width>0&&height>0&&rgb.size()==static_cast<std::size_t>(width)*height*3; } };
inline std::uint16_t u16(const std::vector<std::uint8_t>&b,std::size_t p){return p+2<=b.size()?static_cast<std::uint16_t>(b[p]|(b[p+1]<<8)):0;}
inline std::uint32_t u32(const std::vector<std::uint8_t>&b,std::size_t p){return p+4<=b.size()?static_cast<std::uint32_t>(b[p]|(b[p+1]<<8)|(b[p+2]<<16)|(b[p+3]<<24)):0;}
inline bool decode_bmp(const std::vector<std::uint8_t>& bytes,RgbImage& out,std::string& failure){out={};failure.clear();if(bytes.size()<54||bytes[0]!='B'||bytes[1]!='M'){failure="bmp_header_invalid";return false;}auto off=u32(bytes,10);auto dib=u32(bytes,14);auto w=static_cast<std::int32_t>(u32(bytes,18));auto hs=static_cast<std::int32_t>(u32(bytes,22));auto bpp=u16(bytes,28);auto comp=u32(bytes,30);if(dib<40||w<=0||hs==0||w>4096||hs==INT32_MIN||std::abs(hs)>4096||u16(bytes,26)!=1||comp!=0||(bpp!=24&&bpp!=32)){failure="bmp_format_unsupported";return false;}int h=std::abs(hs);std::size_t stride=((static_cast<std::size_t>(w)*bpp+31)/32)*4;if(off>bytes.size()||stride*static_cast<std::size_t>(h)>bytes.size()-off){failure="bmp_pixels_truncated";return false;}out.width=w;out.height=h;out.rgb.resize(static_cast<std::size_t>(w)*h*3);int ch=bpp/8;for(int y=0;y<h;++y){int sy=hs>0?h-1-y:y;auto*src=bytes.data()+off+stride*sy;auto*dst=out.rgb.data()+static_cast<std::size_t>(y)*w*3;for(int x=0;x<w;++x){dst[x*3]=src[x*ch+2];dst[x*3+1]=src[x*ch+1];dst[x*3+2]=src[x*ch];}}return true;}
inline void sample_bilinear(const RgbImage&im,double u,double v,double&r,double&g,double&b){u=std::clamp(u,0.0,1.0);v=std::clamp(v,0.0,1.0);double x=u*(im.width-1),y=(1-v)*(im.height-1);int x0=static_cast<int>(x),y0=static_cast<int>(y),x1=std::min(im.width-1,x0+1),y1=std::min(im.height-1,y0+1);double tx=x-x0,ty=y-y0;auto c=[&](int px,int py,int k){return im.rgb[(static_cast<std::size_t>(py)*im.width+px)*3+k]/255.0;};auto q=[&](int k){return (c(x0,y0,k)*(1-tx)+c(x1,y0,k)*tx)*(1-ty)+(c(x0,y1,k)*(1-tx)+c(x1,y1,k)*tx)*ty;};r=q(0);g=q(1);b=q(2);}
}
