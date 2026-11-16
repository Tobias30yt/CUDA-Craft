#include "cudacraft/terrain.hpp"
#include "cudacraft/terrain_math.hpp"
#include "cudacraft/gpu.hpp"
#include "cudacraft/world.hpp"
#include "cudacraft/biome_source.hpp"
#include "cudacraft/noise_terrain.hpp"
#include <iostream>
#include <set>
#include <stdexcept>

namespace {
void require(bool condition,const char* message) { if(!condition) throw std::runtime_error(message); }
std::uint64_t checksum(const std::vector<std::uint32_t>& blocks) {
    std::uint64_t result=1469598103934665603ULL;
    for(auto block:blocks) { result^=block; result*=1099511628211ULL; }
    return result;
}
}
int main() {
    try {
        using namespace cudacraft;
        using namespace terrain_ids;
        using namespace terrain_math;
        std::set<int> biomes;
        int low=320,high=-64;
        for(int z=-1024;z<1024;z+=16) for(int x=-1024;x<1024;x+=16) {
            const auto c=column(20261113,x,z); low=std::min(low,c.height); high=std::max(high,c.height); biomes.insert(c.biome);
        }
        require(high-low>40,"Terrain has no substantial relief");
        require(biomes.size()>=7,"Biome diversity missing");
        require(biomes.contains(biome_ocean),"No oceans in default world");
        require(biomes.contains(biome_desert),"No deserts in default world");
        std::cout<<"Default seed: heights "<<low<<".."<<high<<", "<<biomes.size()<<" surface biomes\n";
        int cave_air=0,ore_count=0;
        for(const auto [cx,cz]:{std::pair(0,0),std::pair(-1,-1),std::pair(3,-7)}) {
            const std::int64_t seed=cx==0?20261113:-9223372036854775807LL;
            const auto gpu_blocks=gpu::overworld_terrain(seed,cx,cz);
            for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
                const auto c=column(static_cast<std::uint64_t>(seed),cx*16+x,cz*16+z);
                for(int y=-64;y<320;++y) {
                    const auto i=static_cast<std::size_t>((y+64)*256+z*16+x);
                    const int expected=block(static_cast<std::uint64_t>(seed),cx*16+x,y,cz*16+z,c);
                    require(static_cast<int>(gpu_blocks[i])==expected,"CPU/CUDA terrain mismatch");
                    if(y==-64) require(expected==bedrock,"Broken bedrock floor");
                    if(y<c.height-5 && y> -50 && expected==air) ++cave_air;
                    if(expected==iron_ore || expected==deepslate_iron_ore) ++ore_count;
                }
            }
        }
        require(cave_air>100,"Caves were not generated");
        require(ore_count>0,"Ore deposits were not generated");
        const auto first=generate_overworld(20261113,-1,0);
        (void)generate_overworld(20261113,0,0);
        const auto second=generate_overworld(20261113,-1,0);
        require(first.blocks==second.blocks && first.biomes==second.biomes,"Generation depends on request order");
        require(checksum(first.blocks)!=checksum(generate_overworld(20261114,-1,0).blocks),"Seed does not change terrain");
        TerrainOptions options; options.generator="overworld"; options.seed=20261113;
        World world({},options);
        const auto spawn=world.spawn();
        require(World::valid(spawn),"Spawn outside world");
        const int below=world.block({spawn.x,spawn.y-1,spawn.z});
        require(below!=air && below!=water && below!=lava,"Unsafe spawn floor");
        require(world.block(spawn)==air || world.block(spawn)==short_grass || world.block(spawn)==poppy || world.block(spawn)==dandelion,"Spawn inside a block");
        require(world.set(spawn,stone),"Natural world cannot be edited");
        require(world.block(spawn)==stone,"Natural-world edit missing");
        require(world.block({33554431,64,33554431})==0,"Outside-border access should not generate chunks");
        BiomeSource climate(20261113);
        std::array<Column,256> columns;
        for(int z=0;z<16;++z) for(int x=0;x<16;++x) columns[static_cast<std::size_t>(z*16+x)]=climate.column(x-16,z);
        const auto natural=gpu::climate_terrain(climate.seed(),-1,0,columns);
        for(int z=0;z<16;++z) for(int x=0;x<16;++x) for(int y=-64;y<320;++y) {
            const auto index=static_cast<std::size_t>((y+64)*256+z*16+x);
            require(natural[index]==static_cast<unsigned>(block(20261113,x-16,y,z,columns[static_cast<std::size_t>(z*16+x)])),"Climate CPU/CUDA mismatch");
        }
        const auto climate_a=generate_climate_overworld(climate,-1,0);
        (void)generate_climate_overworld(climate,0,0);
        const auto climate_b=generate_climate_overworld(climate,-1,0);
        require(climate_a.blocks==climate_b.blocks && climate_a.biomes==climate_b.biomes,"Climate chunks depend on generation order");
        options.generator="climate";
        World climate_world({},options);
        const auto climate_spawn=climate_world.spawn();
        require(climate_world.block(climate_spawn)==air || climate_world.block(climate_spawn)==short_grass || climate_world.block(climate_spawn)==poppy || climate_world.block(climate_spawn)==dandelion,"Climate spawn is inside a block");
        std::cout<<"Climate spawn: "<<climate_spawn.x<<','<<climate_spawn.y<<','<<climate_spawn.z<<"; 98304 additional CPU/CUDA blocks matched\n";
        NoiseTerrain density(climate);
        const auto density_a=density.decorated_chunk(-1,0);
        (void)density.decorated_chunk(2,-3);
        const auto density_b=density.decorated_chunk(-1,0);
        require(density_a.blocks==density_b.blocks && density_a.biomes==density_b.biomes,"Density decoration depends on request order");
        options.generator="density";
        World density_world({},options);
        const auto density_spawn=density_world.spawn();
        require(World::valid(density_spawn) && density_spawn.y<319,"Invalid density-world spawn");
        require(density_world.block(density_spawn)==air || density_world.block(density_spawn)==short_grass || density_world.block(density_spawn)==dandelion || density_world.block(density_spawn)==poppy,"Density spawn inside a block");
        require(density_world.block({density_spawn.x,density_spawn.y-1,density_spawn.z})!=water,"Density spawn under water");
        require(density_world.set(density_spawn,stone),"Density world cannot be edited");
        require(density_world.block(density_spawn)==stone,"Density-world edit missing");
        std::cout<<"Density spawn: "<<density_spawn.x<<','<<density_spawn.y<<','<<density_spawn.z<<"; decoration order and edits checked\n";
        std::cout<<"Terrain checks passed: CPU/CUDA parity for 294912 blocks, seed/order determinism, biomes, caves, ores, spawn and edits\n";
        return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
