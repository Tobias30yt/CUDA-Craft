#include "cudacraft/biome_source.hpp"
#include <generator.h>
#include "cudacraft/biome_mapping.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cudacraft {
namespace {
int wire_biome(int id) {
    const int result=biome_mapping::protocol_id(id);
    if(result<0) throw std::runtime_error("Biome source returned an unmapped biome");
    return result;
}
}
struct BiomeSource::Impl {
    Generator generator{};
    std::int64_t seed;
    explicit Impl(std::int64_t value):seed(value) {
        setupGenerator(&generator,MC_1_21_11,0);
        applySeed(&generator,DIM_OVERWORLD,static_cast<std::uint64_t>(value));
    }
};
BiomeSource::BiomeSource(std::int64_t seed):impl_(std::make_unique<Impl>(seed)) {}
BiomeSource::~BiomeSource()=default;
std::int64_t BiomeSource::seed() const { return impl_->seed; }
std::uint64_t BiomeSource::seed_hash() const { return getVoronoiSHA(static_cast<std::uint64_t>(impl_->seed)); }
int BiomeSource::at_quart(int x,int y,int z) const { return wire_biome(getBiomeAt(&impl_->generator,4,x,y,z)); }
std::array<std::uint16_t,1536> BiomeSource::chunk(int cx,int cz) const {
    std::array<std::uint16_t,1536> result{};
    // A fresh chunk has no previous biome lookup. Zero triggers Cubiomes' heuristic
    // below-world warm-up; the adapter sentinel requests an actual cold search.
    std::uint64_t previous=UINT64_MAX;
    for(int sy=-4;sy<20;++sy) {
        int section[4][4][4];
        genBiomeNoiseChunkSection(&impl_->generator.bn,section,cx,sy,cz,&previous);
        for(int x=0;x<4;++x) for(int y=0;y<4;++y) for(int z=0;z<4;++z)
            result[static_cast<std::size_t>(((sy+4)*4+y)*16+z*4+x)]=static_cast<std::uint16_t>(wire_biome(section[x][y][z]));
    }
    return result;
}
terrain_math::Column BiomeSource::column(int x,int z) const {
    const int qx=terrain_math::floor_div(x,4),qz=terrain_math::floor_div(z,4);
    float heights[4];
    if(mapApproxHeight(heights,nullptr,&impl_->generator,nullptr,qx,qz,2,2)!=0)
        throw std::runtime_error("Climate height sampling failed");
    const double tx=(x-qx*4)*0.25,tz=(z-qz*4)*0.25;
    const double a=heights[0]+(heights[1]-heights[0])*tx,b=heights[2]+(heights[3]-heights[2])*tx;
    int height=std::clamp(static_cast<int>(std::floor(a+(b-a)*tz)), -40, 270);
    const int id=getBiomeAt(&impl_->generator,4,qx,terrain_math::floor_div(height,4),qz);
    int top=terrain_ids::grass_block,filler=terrain_ids::dirt;
    if(isOceanic(id) || id==::river || id==::frozen_river) height=std::min(height,60);
    if(height<62) top=terrain_ids::gravel;
    if(id==::desert || id==::beach || id==::snowy_beach) { top=terrain_ids::sand;filler=terrain_ids::sandstone; }
    if(isMesa(id)) { top=terrain_ids::red_sand;filler=terrain_ids::terracotta; }
    if(id==::mushroom_fields) top=terrain_ids::mycelium;
    if(id==::old_growth_pine_taiga || id==::old_growth_spruce_taiga) top=terrain_ids::podzol;
    if(id==::mangrove_swamp) top=terrain_ids::mud;
    if(isSnowy(id) && height>=62) top=terrain_ids::snow_block;
    if((id==::stony_peaks || id==::stony_shore || id==::windswept_gravelly_hills) && height>=62) { top=terrain_ids::stone;filler=terrain_ids::stone; }
    return {height,wire_biome(id),top,filler};
}
}
