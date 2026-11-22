#include "cudacraft/simulation.hpp"
#include "cudacraft/block_physics.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/world.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>
#include <queue>

namespace cudacraft {
namespace {

int skylight(const World& world,BlockPos detector) {
    auto sky_opacity=[&](BlockPos p){return simdata::block_physics(world.block(p)).opacity;};
    // Work out inward attenuation from surrounding columns with open sky.
    std::priority_queue<std::pair<int,BlockPos>,std::vector<std::pair<int,BlockPos>>,std::greater<>> pending;
    std::map<BlockPos,int> visited{{detector,0}};
    pending.emplace(0,detector);
    int best=0;
    while(!pending.empty()) {
        const auto [cost,position]=pending.top();
        pending.pop();
        if(cost+best>=15)break;
        if(cost!=visited.at(position))continue;
        int vertical=15;
        for(int y=position.y;y<world.min_y()+world.height()&&vertical>cost+best;++y)
            vertical-=sky_opacity({position.x,y,position.z});
        best=std::max(best,vertical-cost);
        const int neighbor_cost=cost+std::max(1,sky_opacity(position));
        if(neighbor_cost+best>=15)continue;
        for(const auto offset:std::array<BlockPos,6>{{{0,-1,0},{0,1,0},{-1,0,0},{1,0,0},{0,0,-1},{0,0,1}}}) {
            const BlockPos next{position.x+offset.x,position.y+offset.y,position.z+offset.z};
            if(!World::valid(next)||sky_opacity(next)>=15)continue;
            auto [known,inserted]=visited.try_emplace(next,neighbor_cost);
            if(!inserted&&known->second<=neighbor_cost)continue;
            known->second=neighbor_cost;
            pending.emplace(neighbor_cost,next);
        }
    }
    return best;
}

int sky_darkening(int time) {
    // Clear-weather game light from the 1.21.11 Overworld day timeline.
    const auto dusk=std::clamp((time-11867)/1803.0,0.0,1.0);
    const int dawn_tick=time<133?time+24000:time;
    const auto dawn=std::clamp((dawn_tick-22330)/1803.0,0.0,1.0);
    return static_cast<int>(std::round(11*(time<133||time>=22330?1-dawn:dusk)));
}

double sun_angle(int time) {
    const double phase=(time+18000)%24000/24000.0;
    auto curve=[](double t,double a,double b) {
        const auto u=1-t;
        return t*(3*u*u*a+3*t*u*b+t*t);
    };
    double low=0,high=1;
    for(int i=0;i<24;++i) {
        const double mid=(low+high)/2;
        if(curve(mid,.362,.638)<phase)low=mid;
        else high=mid;
    }
    return curve((low+high)/2,.241,.759)*2*std::numbers::pi;
}

}

void Simulation::update_daylight(BlockPos position) {
    using namespace simdata;
    if(world_.dimension_id()!=0)return;
    const int state=world_.block(position);
    if(std::string_view(block(state).name)!="daylight_detector")return;
    const int time=static_cast<int>(day_time%24000);
    int signal=skylight(world_,position)-sky_darkening(time);
    if(property_value(state,"inverted")=="true")signal=15-signal;
    else if(signal>0) {
        const auto angle=sun_angle(time);
        const auto nearest_noon=angle<std::numbers::pi?0:2*std::numbers::pi;
        signal=static_cast<int>(std::round(signal*std::cos(angle+(nearest_noon-angle)*.2)));
    }
    const int next=with_property(state,"power",std::to_string(std::clamp(signal,0,15)));
    if(next!=state&&world_.set(position,next))redstone();
}

}
