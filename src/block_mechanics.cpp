#include "cudacraft/world.hpp"
#include "cudacraft/simulation.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/terrain_math.hpp"
#include "cudacraft/game.hpp"
#include <string>

namespace cudacraft {
namespace {
bool gravity(std::string_view n){return n=="sand"||n=="red_sand"||n=="gravel"||n.ends_with("concrete_powder")||n=="anvil"||n=="chipped_anvil"||n=="damaged_anvil"||n=="suspicious_sand"||n=="suspicious_gravel";}
bool crop(std::string_view n){return n=="wheat"||n=="carrots"||n=="potatoes"||n=="beetroots"||n=="nether_wart"||n=="sweet_berry_bush"||n=="cocoa"||n=="torchflower_crop"||n=="pitcher_crop";}
int state(std::string_view name){for(const auto& b:simdata::blocks)if(name==b.name)return b.state;return 0;}
}
bool Simulation::ticking_block(int s){const std::string_view n=simdata::block(s).name;return gravity(n)||crop(n)||n=="farmland"||n.ends_with("_pressure_plate");}
bool Simulation::fertilize(BlockPos p) {
    const int s=world_.block(p);const std::string_view n=simdata::block(s).name;
    if(!crop(n)||n=="nether_wart")return false;
    const auto* age=simdata::property(s,"age");if(!age)return false;
    const int current=std::stoi(std::string(simdata::property_value(s,"age")));
    const int next=std::min(age->count-1,current+2);if(next==current)return false;
    return world_.set(p,simdata::with_property(s,"age",std::to_string(next)));
}
void Simulation::tick_blocks() {
    std::vector<BlockPos> releases;for(auto [p,due]:button_releases_)if(due<=tick)releases.push_back(p);
    for(auto p:releases){button_releases_.erase(p);world_.set(p,simdata::with_property(world_.block(p),"powered","false"));}
    // Bounded round-robin work avoids starving positions late in the map.
    std::vector<BlockPos> batch;auto cursor=ticking_blocks_.upper_bound(block_cursor_);
    if(cursor==ticking_blocks_.end())cursor=ticking_blocks_.begin();
    const auto count=std::min<std::size_t>(512,ticking_blocks_.size());
    for(std::size_t i=0;i<count;++i){batch.push_back(*cursor);if(++cursor==ticking_blocks_.end())cursor=ticking_blocks_.begin();}
    for(auto p:batch) {
        block_cursor_=p;const int s=world_.block(p);const std::string_view n=simdata::block(s).name;
        if(!ticking_block(s)){ticking_blocks_.erase(p);continue;}
        if(n.ends_with("_pressure_plate")) {
            int entities=0;auto inside=[&](double x,double y,double z){return x>p.x-.2&&x<p.x+1.2&&z>p.z-.2&&z<p.z+1.2&&y>=p.y&&y<p.y+.5;};
            for(auto* player:world_.players)if(player->alive()&&inside(player->x,player->y,player->z))++entities;
            for(const auto& [id,m]:mobs)if((m.type!=71||n!="stone_pressure_plate")&&inside(m.x,m.y,m.z))++entities;
            int next=simdata::with_property(s,"powered",entities?"true":"false");
            next=simdata::with_property(next,"power",std::to_string(std::min(15,n=="heavy_weighted_pressure_plate"?(entities+9)/10:entities)));
            if(next!=s)world_.set(p,next);continue;
        }
        if(gravity(n)) {
            if(n.ends_with("_concrete_powder")) {
                bool water=false;for(BlockPos d:std::initializer_list<BlockPos>{{0,-1,0},{0,1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}})water|=simdata::water(world_.block({p.x+d.x,p.y+d.y,p.z+d.z}));
                if(water){world_.set(p,state(std::string(n.substr(0,n.size()-7))));continue;}
            }
            BlockPos down{p.x,p.y-1,p.z};int below=world_.block(down);
            if(down.y>world_.min_y()&&(below==0||simdata::water(below)||simdata::lava(below)))world_.set_blocks({{p,0},{down,s}});
            continue;
        }
        if(tick%20)continue;
        if(n=="farmland") {
            bool water=false;for(int x=-4;x<=4&&!water;++x)for(int z=-4;z<=4&&!water;++z)for(int y=0;y<=1;++y)water|=simdata::water(world_.block({p.x+x,p.y+y,p.z+z}));
            const int moisture=std::stoi(std::string(simdata::property_value(s,"moisture")));
            if(water)world_.set(p,simdata::with_property(s,"moisture","7"));
            else if(moisture)world_.set(p,simdata::with_property(s,"moisture",std::to_string(moisture-1)));
            else if(!crop(simdata::block(world_.block({p.x,p.y+1,p.z})).name))world_.set(p,state("dirt"));
        } else if(crop(n)&&terrain_math::hash(static_cast<std::uint64_t>(world_.options().seed)+tick/20,p.x,p.y,p.z)%12==0) {
            const auto* age=simdata::property(s,"age");if(!age)continue;
            int current=std::stoi(std::string(simdata::property_value(s,"age")));
            if(current<age->count-1)world_.set(p,simdata::with_property(s,"age",std::to_string(current+1)));
        }
    }
}
}
