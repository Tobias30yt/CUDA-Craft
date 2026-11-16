#include "cudacraft/terrain.hpp"
#include "cudacraft/biome_source.hpp"
#include "cudacraft/gpu.hpp"
#include "cudacraft/decorations.hpp"
#include <algorithm>

namespace cudacraft {
namespace {
struct Tree { int log,leaves,chance,height; bool conifer=false; };
Tree tree_for(int biome) {
    using namespace terrain_ids;
    switch(biome) {
    case biome_pale_garden: return {pale_oak_log,pale_oak_leaves,1,7};
    case biome_cherry_grove: return {cherry_log,cherry_leaves,2,6};
    case biome_birch_forest: case biome_old_growth_birch_forest: return {birch_log,birch_leaves,2,6};
    case biome_jungle: case biome_bamboo_jungle: return {jungle_log,jungle_leaves,1,10};
    case biome_sparse_jungle: return {jungle_log,jungle_leaves,4,8};
    case biome_dark_forest: return {dark_oak_log,dark_oak_leaves,1,7};
    case biome_mangrove_swamp: return {mangrove_log,mangrove_leaves,2,6};
    case biome_taiga: case biome_snowy_taiga: case biome_old_growth_pine_taiga:
    case biome_old_growth_spruce_taiga: case biome_grove: return {spruce_log,spruce_leaves,2,8,true};
    case biome_savanna: case biome_savanna_plateau: case biome_windswept_savanna: return {acacia_log,acacia_leaves,6,5};
    case biome_forest: case biome_flower_forest: case biome_windswept_forest: return {oak_log,oak_leaves,2,5};
    case biome_swamp: return {oak_log,oak_leaves,4,5};
    case biome_plains: case biome_sunflower_plains: case biome_meadow: return {oak_log,oak_leaves,16,5};
    default: return {0,0,1,0};
    }
}
bool leaves(int state) {
    using namespace terrain_ids;
    return state==oak_leaves || state==birch_leaves || state==spruce_leaves || state==jungle_leaves || state==dark_oak_leaves ||
        state==acacia_leaves || state==mangrove_leaves || state==cherry_leaves || state==pale_oak_leaves;
}
}
TerrainChunk generate_climate_overworld(const BiomeSource& source,int cx,int cz) {
    using namespace terrain_ids;
    using namespace terrain_math;
    std::array<Column,256> columns;
    for(int z=0;z<16;++z) for(int x=0;x<16;++x) columns[static_cast<std::size_t>(z*16+x)]=source.column(cx*16+x,cz*16+z);
    TerrainChunk result;
    result.blocks=gpu::climate_terrain(source.seed(),cx,cz,columns);
    result.biomes=source.chunk(cx,cz);
    decorate_terrain(result,source.seed(),cx,cz,columns,[&](int x,int z){return source.column(x,z);});
    return result;
}
void decorate_terrain(TerrainChunk& result,std::int64_t world_seed,int cx,int cz,
    const std::array<terrain_math::Column,256>& columns,
    const std::function<terrain_math::Column(int,int)>& column) {
    using namespace terrain_ids;
    using namespace terrain_math;
    const auto seed=static_cast<std::uint64_t>(world_seed);
    auto put=[&](int x,int y,int z,int state,bool foliage=false) {
        if(x<cx*16 || x>=cx*16+16 || z<cz*16 || z>=cz*16+16 || y< -64 || y>=320) return;
        auto& target=result.blocks[static_cast<std::size_t>((y+64)*256+(z-cz*16)*16+x-cx*16)];
        if(target==air || (!foliage && leaves(static_cast<int>(target)))) target=static_cast<std::uint32_t>(state);
    };
    // Approximate decorations, deliberately separate from verified biome sampling.
    // Global anchors with a 4-block canopy halo keep chunk order irrelevant.
    for(int gz=floor_div(cz*16-4,8);gz<=floor_div(cz*16+19,8);++gz)
    for(int gx=floor_div(cx*16-4,8);gx<=floor_div(cx*16+19,8);++gx) {
        const auto h=hash(seed+20,gx,0,gz);
        const int x=gx*8+static_cast<int>(h%6)+1,z=gz*8+static_cast<int>((h>>8)%6)+1;
        const auto c=column(x,z);const auto tree=tree_for(c.biome);
        if(!tree.log || c.height<63 || h%static_cast<unsigned>(tree.chance)!=0) continue;
        if(abs_i(column(x-1,z).height-c.height)>2 || abs_i(column(x+1,z).height-c.height)>2 ||
           abs_i(column(x,z-1).height-c.height)>2 || abs_i(column(x,z+1).height-c.height)>2) continue;
        const int top=c.height+tree.height+static_cast<int>((h>>16)%3);
        for(int yy=top-(tree.conifer?5:3);yy<=top+1;++yy) {
            const int radius=yy>=top?1:tree.conifer?std::min(3,1+(top-yy)/2):3;
            for(int dz=-radius;dz<=radius;++dz) for(int dx=-radius;dx<=radius;++dx)
                if(abs_i(dx)+abs_i(dz)<=radius+1) put(x+dx,yy,z+dz,tree.leaves,true);
        }
        for(int yy=c.height+1;yy<=top;++yy) put(x,yy,z,tree.log);
    }
    for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
        const auto c=columns[static_cast<std::size_t>(z*16+x)];
        if(c.biome==biome_frozen_ocean || c.biome==biome_deep_frozen_ocean || c.biome==biome_frozen_river) {
            auto& top=result.blocks[static_cast<std::size_t>((62+64)*256+z*16+x)];
            if(top==water) top=ice;
        }
        if(c.top!=grass_block || c.height<63) continue;
        const auto h=hash(seed+21,cx*16+x,0,cz*16+z);
        if(h%7==0) put(cx*16+x,c.height+1,cz*16+z,h%49==0?dandelion:h%49==7?poppy:short_grass);
    }
}
}
