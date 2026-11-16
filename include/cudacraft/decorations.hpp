#pragma once
#include "cudacraft/terrain.hpp"
#include "cudacraft/terrain_math.hpp"
#include <functional>

namespace cudacraft {
// Temporary, explicitly approximate vegetation shared by the 0.5/0.6 worlds.
void decorate_terrain(TerrainChunk& result, std::int64_t seed, int cx, int cz,
    const std::array<terrain_math::Column,256>& columns,
    const std::function<terrain_math::Column(int,int)>& column);
}
