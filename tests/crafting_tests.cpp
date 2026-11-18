#include "cudacraft/crafting.hpp"
#include "cudacraft/crafting_data.hpp"
#include "cudacraft/simulation_data.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
using namespace cudacraft;
namespace sd=cudacraft::simdata;
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
int main(){try{
    for(const auto& recipe:crafting::recipes) {
        for(bool mirror:{false,true}) {
            std::array<ItemStack,9> grid{};
            for(int i=0;i<9;++i)if(recipe.slots[i]) {
                const auto& group=crafting::ingredients[recipe.slots[i]];
                int slot=recipe.width?(i/recipe.width)*3+(mirror?recipe.width-1-i%recipe.width:i%recipe.width):mirror?8-i:i;
                grid[slot]={crafting::ingredient_items[group.offset],1,0};
            }
            const auto* found=crafting::match(grid,3);
            if(!found||found->output!=recipe.output||found->count!=recipe.count)throw std::runtime_error(std::string("Recipe mismatch: ")+recipe.name);
            ++checks;
        }
    }
    auto stack=[](const char* name,int count=1){return ItemStack{sd::item_id(name),count,0};};
    std::array<ItemStack,9> grid{};
    grid[3]=stack("stripped_dark_oak_wood");auto* result=crafting::match(grid,2);
    check(result&&result->output==sd::item_id("dark_oak_planks")&&result->count==4,"log tags include stripped wood in offset 2x2 grid");
    grid={};grid[1]=stack("oak_planks");grid[3]=stack("spruce_planks");result=crafting::match(grid,2);
    check(result&&result->output==sd::item_id("stick")&&result->count==4,"shaped tags allow mixed plank variants and shifted pattern");
    grid[0]=stack("diamond");check(!crafting::match(grid,2),"unrelated extra ingredient rejects recipe");
    grid={};grid[0]=grid[1]=grid[2]=stack("oak_planks");grid[3]=grid[5]=stack("oak_planks");grid[6]=grid[7]=grid[8]=stack("oak_planks");
    check(crafting::match(grid,3)->output==sd::item_id("chest"),"chest pattern uses an empty center");
    grid[4]=stack("oak_planks");check(!crafting::match(grid,3),"filled chest center is not accepted");
    grid={};grid[0]=stack("bowl");grid[1]=stack("red_mushroom");grid[2]=stack("brown_mushroom");grid[3]=stack("wither_rose");
    check(!crafting::match(grid,2),"effect-bearing suspicious stew is explicitly unsupported");
    grid={};grid[0]=stack("honey_bottle",8);result=crafting::match(grid,2);
    check(result&&result->output==sd::item_id("sugar")&&result->count==3,"shapeless honey recipe yields three sugar");
    check(crafting::remainder(sd::item_id("milk_bucket"))==sd::bucket_item,"milk returns a crafting bucket");
    check(crafting::remainder(sd::item_id("honey_bottle"))==sd::item_id("glass_bottle"),"honey returns a crafting bottle");
    check(!crafting::remainder(sd::item_id("powder_snow_bucket")),"use remainder is not mistaken for crafting remainder");
    std::cout<<checks<<" crafting recipe checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
