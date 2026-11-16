#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include <span>
#include "cudacraft/terrain_math.hpp"
namespace cudacraft::gpu {
// Returns 0 on success, 77 when no CUDA backend/device is available, 1 on failure.
int self_test(std::string& detail);
bool compiled();
std::vector<std::uint32_t> flat_terrain();
std::vector<std::uint32_t> overworld_terrain(std::int64_t seed,int cx,int cz);
std::vector<std::uint32_t> climate_terrain(std::int64_t seed,int cx,int cz,std::span<const terrain_math::Column> columns);
}
