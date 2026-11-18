#include "cudacraft/crafting.hpp"
#include "cudacraft/crafting_data.hpp"
#include "cudacraft/game.hpp"
#include "cudacraft/simulation_data.hpp"
#include <algorithm>
#include <bit>
#include <cmath>

namespace cudacraft::crafting {
namespace {
bool accepts(int group,int item) {
    if(!group)return item==0;
    const auto& g=ingredients[group];const int* first=ingredient_items+g.offset;
    return std::binary_search(first,first+g.count,item);
}
}
const Recipe* match(const std::array<ItemStack,9>& grid,int width) {
    if(width!=2&&width!=3)return nullptr;
    int minx=width,miny=width,maxx=-1,maxy=-1,count=0;std::array<int,9> actual{};
    for(int i=0;i<width*width;++i)if(grid[i].item&&grid[i].count>0) {
        minx=std::min(minx,i%width);maxx=std::max(maxx,i%width);miny=std::min(miny,i/width);maxy=std::max(maxy,i/width);actual[count++]=grid[i].item;
    }
    if(!count)return nullptr;
    for(const auto& recipe:recipes) {
        if(recipe.occupied!=count)continue;
        if(recipe.width) {
            if(recipe.width!=maxx-minx+1||recipe.height!=maxy-miny+1)continue;
            for(bool mirror:{false,true}) {
                bool matches=true;
                for(int y=0;y<recipe.height&&matches;++y)for(int x=0;x<recipe.width;++x)
                    if(!accepts(recipe.slots[y*recipe.width+(mirror?recipe.width-1-x:x)],grid[(miny+y)*width+minx+x].item)){matches=false;break;}
                if(matches)return &recipe;
            }
        } else {
            // Ingredient tags can overlap. Bipartite matching avoids consuming an
            // item needed by a later, more specific ingredient in a greedy match.
            std::array<bool,512> possible{};possible[0]=true;const int end=(1<<count)-1;
            for(int mask=0;mask<end;++mask)if(possible[mask]) {
                const int ingredient=recipe.slots[std::popcount(static_cast<unsigned>(mask))];
                for(int j=0;j<count;++j)if(!(mask&(1<<j))&&accepts(ingredient,actual[j]))possible[mask|(1<<j)]=true;
            }
            if(possible[end])return &recipe;
        }
    }
    return nullptr;
}
}
namespace cudacraft {
namespace {
bool same(ItemStack a,ItemStack b){return a.item==b.item&&a.damage==b.damage;}
void insert(std::array<ItemStack,46>& inventory,ItemStack& stack,bool reverse) {
    const int limit=simdata::item(stack.item)->stack;
    for(int pass=0;pass<2;++pass)for(int n=0;n<36&&stack.count;++n) {
        const int i=reverse?44-n:n<9?36+n:n;auto& slot=inventory[i];
        if((pass==0&&slot.item&&same(slot,stack))||(pass==1&&!slot.item)) {
            const int moved=std::min(stack.count,limit-slot.count);
            slot={stack.item,slot.count+moved,stack.damage};stack.count-=moved;
        }
    }
}
}
void GameSession::update_recipe() {
    craft_result_={};craft_slots_.clear();
    const int width=crafting_open_?3:2;const auto* recipe=crafting::match(crafting_,width);if(!recipe)return;
    craft_result_={recipe->output,recipe->count,0};
    for(int i=0;i<width*width;++i)if(crafting_[i].item)craft_slots_.push_back(i);
}
bool GameSession::craft_output(bool quick) {
    if(!craft_result_.item||craft_slots_.empty())return false;
    auto inventory=inventory_;auto grid=crafting_;auto cursor=cursor_;std::vector<ItemStack> drops;
    if(quick) {
        auto output=craft_result_;insert(inventory,output,true);
        if(output.count==craft_result_.count)return false;
        // Vanilla takes one recipe if any output fits, dropping the remainder.
        if(output.count)drops.push_back(output);
    } else {
        if((cursor.item&&!same(cursor,craft_result_))||cursor.count+craft_result_.count>simdata::item(craft_result_.item)->stack)return false;
        cursor={craft_result_.item,cursor.count+craft_result_.count,0};
    }
    for(int i:craft_slots_) {
        const int remainder=crafting::remainder(grid[i].item);
        if(--grid[i].count==0)grid[i]={};
        if(!remainder)continue;
        ItemStack rest{remainder,1,0};
        if(!grid[i].item)grid[i]=rest;
        else if(same(grid[i],rest)&&grid[i].count<simdata::item(remainder)->stack)++grid[i].count;
        else {insert(inventory,rest,false);if(rest.count)drops.push_back(rest);}
    }
    auto& simulation=world_->simulation();
    if(simulation.mobs.size()+drops.size()>4096)return false;
    std::vector<int> spawned;
    for(auto drop:drops) {
        const int id=simulation.spawn(71,x,std::clamp(y+.5,static_cast<double>(world_->min_y()),static_cast<double>(world_->min_y()+world_->height()-1)),z,drop);
        if(!id){for(int previous:spawned)simulation.mobs.erase(previous);return false;}
        spawned.push_back(id);
    }
    inventory_=inventory;crafting_=grid;cursor_=cursor;world_->mark_dirty();return true;
}
}
