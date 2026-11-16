#pragma once
#include "cudacraft/terrain_math.hpp"
#include <array>
#include <cstdint>
#include <memory>

namespace cudacraft {
// Native 1.21.11 climate/biome sampling; terrain height is explicitly approximate.
class BiomeSource {
public:
    explicit BiomeSource(std::int64_t seed);
    ~BiomeSource();
    BiomeSource(const BiomeSource&)=delete;
    BiomeSource& operator=(const BiomeSource&)=delete;
    std::array<std::uint16_t,1536> chunk(int cx,int cz) const;
    int at_quart(int x,int y,int z) const;
    terrain_math::Column column(int x,int z) const;
    std::uint64_t seed_hash() const;
    std::int64_t seed() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
