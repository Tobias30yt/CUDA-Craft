#include "cudacraft/terrain.hpp"
#include "cudacraft/terrain_math.hpp"
#include "cudacraft/gpu.hpp"
#include <algorithm>

namespace cudacraft {
TerrainChunk generate_overworld(std::int64_t seed,int cx,int cz) {
    using namespace terrain_ids;
    using namespace terrain_math;
    const auto s=static_cast<std::uint64_t>(seed);
    TerrainChunk result;
    result.blocks=gpu::overworld_terrain(seed,cx,cz);
    for(int qy=0;qy<96;++qy) for(int qz=0;qz<4;++qz) for(int qx=0;qx<4;++qx) {
        const auto c=column(s,cx*16+qx*4+2,cz*16+qz*4+2);
        result.biomes[static_cast<std::size_t>(qy*16+qz*4+qx)]=static_cast<std::uint16_t>(qy<12?biome_dripstone_caves:c.biome);
    }
    auto put=[&](int x,int y,int z,int state,bool leaves=false) {
        if(x<cx*16 || x>=cx*16+16 || z<cz*16 || z>=cz*16+16 || y< -64 || y>=320) return;
        auto& target=result.blocks[static_cast<std::size_t>((y+64)*256+(z-cz*16)*16+x-cx*16)];
        if(target==air || (!leaves && (target==oak_leaves || target==birch_leaves || target==spruce_leaves))) target=static_cast<std::uint32_t>(state);
    };
    // Global feature cells plus a canopy halo make decorations independent of chunk order.
    for(int gz=floor_div(cz*16-4,8);gz<=floor_div(cz*16+19,8);++gz)
    for(int gx=floor_div(cx*16-4,8);gx<=floor_div(cx*16+19,8);++gx) {
        const auto h=hash(s+20,gx,0,gz);
        const int x=gx*8+static_cast<int>(h%6)+1,z=gz*8+static_cast<int>((h>>8)%6)+1;
        const auto c=column(s,x,z);
        const bool spruce=c.biome==biome_taiga,birch=c.biome==biome_birch_forest;
        const bool forest=c.biome==biome_forest || spruce || birch;
        if(c.height<=64 || c.height>135 || (!forest && (c.biome!=biome_plains || h%12!=0))) continue;
        if(forest && h%5==0) continue;
        // Do not plant on cliffs; all four trunk-adjacent columns must be close.
        if(abs_i(column(s,x-1,z).height-c.height)>2 || abs_i(column(s,x+1,z).height-c.height)>2 ||
           abs_i(column(s,x,z-1).height-c.height)>2 || abs_i(column(s,x,z+1).height-c.height)>2) continue;
        const int trunk=spruce?spruce_log:birch?birch_log:oak_log;
        const int leaf=spruce?spruce_leaves:birch?birch_leaves:oak_leaves;
        const int top=c.height+5+static_cast<int>((h>>16)%3);
        for(int yy=top-3;yy<=top+1;++yy) {
            const int radius=yy>=top?1:2;
            for(int dz=-radius;dz<=radius;++dz) for(int dx=-radius;dx<=radius;++dx)
                if(abs_i(dx)+abs_i(dz)<=radius+1) put(x+dx,yy,z+dz,leaf,true);
        }
        for(int yy=c.height+1;yy<=top;++yy) put(x,yy,z,trunk);
    }
    for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
        const auto c=column(s,cx*16+x,cz*16+z);
        if(c.biome!=biome_plains && c.biome!=biome_forest && c.biome!=biome_birch_forest) continue;
        const auto h=hash(s+21,cx*16+x,0,cz*16+z);
        if(h%7==0) put(cx*16+x,c.height+1,cz*16+z,h%49==0?dandelion:h%49==7?poppy:short_grass);
    }
    return result;
}
}
