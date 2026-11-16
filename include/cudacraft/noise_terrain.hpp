#pragma once
#include "cudacraft/terrain.hpp"
#include "cudacraft/terrain_math.hpp"
#include <memory>

namespace cudacraft {
class BiomeSource;
// 1.21.11 density router, aquifers, ore veins and surface rules.
// Includes cave/canyon carvers. Structures, placed features and fluid promotion
// are separate. This per-world generator is used only by the main server thread.
class NoiseTerrain {
public:
    explicit NoiseTerrain(const BiomeSource& biomes);
    ~NoiseTerrain();
    NoiseTerrain(const NoiseTerrain&) = delete;
    NoiseTerrain& operator=(const NoiseTerrain&) = delete;
    TerrainChunk chunk(int cx, int cz, bool carve = true) const;
    TerrainChunk decorated_chunk(int cx, int cz) const;
    terrain_math::Column column(int x, int z) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
