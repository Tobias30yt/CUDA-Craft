#pragma once
#include "cudacraft/terrain_ids.hpp"
#include <cstdint>

#ifdef __CUDACC__
#define CC_TERRAIN_HD __host__ __device__
#else
#define CC_TERRAIN_HD
#endif

// Original experimental terrain. NOT Minecraft's noise router or vanilla seed parity.
// Integer interpolation makes CPU and CUDA outputs identical, including negatives.
namespace cudacraft::terrain_math {
CC_TERRAIN_HD inline std::uint64_t mix(std::uint64_t x) {
    x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL;
    x=(x^(x>>27))*0x94d049bb133111ebULL;
    return x^(x>>31);
}
CC_TERRAIN_HD inline std::uint64_t hash(std::uint64_t seed,int x,int y,int z) {
    return mix(seed ^ mix(static_cast<std::uint64_t>(x)+0x9e3779b97f4a7c15ULL)
        ^ mix(static_cast<std::uint64_t>(y)+0xd1b54a32d192ed03ULL)
        ^ mix(static_cast<std::uint64_t>(z)+0x94d049bb133111ebULL));
}
CC_TERRAIN_HD inline int floor_div(int x,int d) { return x/d-(x%d<0?1:0); }
CC_TERRAIN_HD inline int abs_i(int x) { return x<0?-x:x; }
CC_TERRAIN_HD inline int clamp(int x,int low,int high) { return x<low?low:x>high?high:x; }
CC_TERRAIN_HD inline int lerp(int a,int b,int t) { return a+static_cast<int>(static_cast<std::int64_t>(b-a)*t/4096); }
CC_TERRAIN_HD inline int smooth(int t) { return static_cast<int>(static_cast<std::int64_t>(t)*t*(12288-2*t)/(4096LL*4096)); }
CC_TERRAIN_HD inline int value(std::uint64_t seed,int x,int y,int z) { return static_cast<int>(hash(seed,x,y,z)>>48)-32768; }
CC_TERRAIN_HD inline int noise(std::uint64_t seed,int x,int y,int z,int scale) {
    const int ix=floor_div(x,scale),iy=floor_div(y,scale),iz=floor_div(z,scale);
    const int tx=smooth((x-ix*scale)*4096/scale),ty=smooth((y-iy*scale)*4096/scale),tz=smooth((z-iz*scale)*4096/scale);
    const int a=lerp(value(seed,ix,iy,iz),value(seed,ix+1,iy,iz),tx);
    const int b=lerp(value(seed,ix,iy,iz+1),value(seed,ix+1,iy,iz+1),tx);
    const int c=lerp(value(seed,ix,iy+1,iz),value(seed,ix+1,iy+1,iz),tx);
    const int d=lerp(value(seed,ix,iy+1,iz+1),value(seed,ix+1,iy+1,iz+1),tx);
    return lerp(lerp(a,b,tz),lerp(c,d,tz),ty);
}
struct Column { int height,biome; int top=-1,filler=-1; };
CC_TERRAIN_HD inline Column column(std::uint64_t seed,int x,int z) {
    using namespace terrain_ids;
    const int continental=noise(seed+1,x,0,z,384);
    const int hills=noise(seed+2,x,0,z,96);
    const int detail=noise(seed+3,x,0,z,24);
    const int ridge=32768-abs_i(noise(seed+4,x,0,z,192));
    const int mountains=clamp(noise(seed+5,x,0,z,256)-3000,0,26000);
    int height=68+continental*42/32768+hills*14/32768+detail*3/32768;
    height+=static_cast<int>(static_cast<std::int64_t>(mountains)*ridge*130/(32768LL*32768));
    const int river=abs_i(noise(seed+6,x,0,z,256));
    if(river<1600 && continental> -11000 && height<110) height=lerp(58,height,river*4096/1600);
    height=clamp(height,35,230);
    const int temperature=noise(seed+7,x,0,z,256),moisture=noise(seed+8,x,0,z,256);
    int biome=biome_plains;
    if(height<59) biome=biome_ocean;
    else if(height<65) biome=biome_beach;
    else if(height>135) biome=biome_snowy_slopes;
    else if(temperature< -10000) biome=moisture>0?biome_taiga:biome_snowy_plains;
    else if(temperature>8000 && moisture< -3000) biome=biome_desert;
    else if(moisture>7000) biome=biome_forest;
    else if(moisture>1000) biome=biome_birch_forest;
    return {height,biome};
}
CC_TERRAIN_HD inline int block(std::uint64_t seed,int x,int y,int z,Column c) {
    using namespace terrain_ids;
    if(y>c.height) return y<=62?water:air;
    if(y==-64 || (y< -60 && hash(seed+9,x,y,z)%5<static_cast<unsigned>(-59-y))) return bedrock;
    // Two intersecting fields form connected tubes; a coarse field opens caverns.
    if(y< c.height-5 && y> -60) {
        const int a=abs_i(noise(seed+10,x,y,z,32));
        const int b=abs_i(noise(seed+11,x,y,z,32));
        if((a<2400 && b<3500) || noise(seed+12,x,y,z,48)>19000) return y< -54?lava:air;
    }
    const bool sandy=c.biome==biome_desert || c.biome==biome_beach;
    const bool snowy=c.biome==biome_snowy_plains || c.biome==biome_snowy_slopes;
    if(y==c.height) return c.top>=0?c.top:sandy?sand:snowy?snow_block:c.height<63?gravel:grass_block;
    if(y>=c.height-3) return c.filler>=0?c.filler:sandy?sandstone:dirt;
    const bool deep=y<0;
    // Small coordinate-stable deposits. Distribution is experimental, not vanilla.
    const auto deposit=hash(seed+13,floor_div(x,3),floor_div(y,3),floor_div(z,3))%1000;
    if(deposit<18 && y>0 && y<130) return coal_ore;
    if(deposit>=18 && deposit<31 && y<64) return deep?deepslate_iron_ore:iron_ore;
    if(deposit>=31 && deposit<36 && y<32) return deep?deepslate_gold_ore:gold_ore;
    if(deposit==36 && y< -16) return deepslate_diamond_ore;
    return deep?deepslate:stone;
}
}
#undef CC_TERRAIN_HD
