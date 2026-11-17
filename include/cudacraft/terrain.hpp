#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace cudacraft {
// Generator identities are persisted; changing an algorithm requires a new identity.
struct TerrainOptions {
    std::string generator = "flat";
    std::int64_t seed = 0;
    bool explicit_generator = false;
    bool explicit_seed = false;
    int gamemode = 1;
};
struct TerrainChunk {
    std::vector<std::uint32_t> blocks;
    std::array<std::uint16_t, 1536> biomes{};
};
TerrainChunk generate_overworld(std::int64_t seed, int cx, int cz);
TerrainChunk generate_dimension(std::int64_t seed,int cx,int cz,int dimension);
class BiomeSource;
TerrainChunk generate_climate_overworld(const BiomeSource& source,int cx,int cz);
}
