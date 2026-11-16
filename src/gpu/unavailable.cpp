#include "cudacraft/gpu.hpp"
#include "cudacraft/terrain_math.hpp"
#include <stdexcept>
namespace cudacraft::gpu {
bool compiled() { return false; }
std::vector<std::uint32_t> climate_terrain(std::int64_t seed,int cx,int cz,std::span<const terrain_math::Column> columns) {
    if(columns.size()!=256) throw std::runtime_error("Expected 256 climate columns");
    std::vector<std::uint32_t> blocks(16*384*16);
    for(int z=0;z<16;++z) for(int x=0;x<16;++x) for(int y=-64;y<320;++y)
        blocks[static_cast<std::size_t>((y+64)*256+z*16+x)]=terrain_math::block(static_cast<std::uint64_t>(seed),cx*16+x,y,cz*16+z,columns[static_cast<std::size_t>(z*16+x)]);
    return blocks;
}
std::vector<std::uint32_t> overworld_terrain(std::int64_t seed,int cx,int cz) {
    std::vector<std::uint32_t> blocks(16*384*16);
    for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
        const auto column=terrain_math::column(static_cast<std::uint64_t>(seed),cx*16+x,cz*16+z);
        for(int y=-64;y<320;++y) blocks[static_cast<std::size_t>((y+64)*256+z*16+x)]=
            terrain_math::block(static_cast<std::uint64_t>(seed),cx*16+x,y,cz*16+z,column);
    }
    return blocks;
}
std::vector<std::uint32_t> flat_terrain() {
    std::vector<std::uint32_t> blocks(16*384*16);
    for (std::size_t i=0;i<blocks.size();++i) {
        const int y=static_cast<int>(i/256)-64;
        blocks[i]=y==-64?85u:y<60?1u:y<63?10u:y==63?9u:0u;
    }
    return blocks;
}
int self_test(std::string& detail) {
    detail = "CUDA backend not compiled. Install the CUDA Toolkit and configure with -DCUDACRAFT_CUDA=ON.";
    return 77;
}
}
