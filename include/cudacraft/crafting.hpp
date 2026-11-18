#pragma once
#include "cudacraft/simulation.hpp"
namespace cudacraft::crafting {
struct Recipe;
const Recipe* match(const std::array<ItemStack,9>& grid,int width);
}
