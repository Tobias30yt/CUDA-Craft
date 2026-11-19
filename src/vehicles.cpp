#include "cudacraft/simulation.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/world.hpp"
#include <algorithm>
#include <cmath>

namespace cudacraft {
namespace {
bool rail(int state) {
    const std::string_view name=simdata::block(state).name;
    return name=="rail"||name=="powered_rail"||name=="activator_rail"||name=="detector_rail";
}

int inventory_strength(const Vehicle& vehicle) {
    const int slots=Simulation::vehicle_slots(vehicle.type);
    float fullness=0;
    for(int slot=0;slot<slots;++slot) {
        const auto& stack=vehicle.items[slot];
        if(const auto* item=simdata::item(stack.item))fullness+=static_cast<float>(stack.count)/item->stack;
    }
    return fullness>0?1+static_cast<int>(std::floor(fullness/slots*14)):0;
}
}

const Vehicle* Simulation::detector_vehicle(BlockPos position) const {
    for(const auto& [id,vehicle]:vehicles) {
        (void)id;
        if((vehicle.type==25||vehicle.type==65)&&std::abs(vehicle.x-position.x-.5)<.79&&
           std::abs(vehicle.z-position.z-.5)<.79&&vehicle.y<position.y+.8&&vehicle.y+.7>position.y)
            return &vehicle;
    }
    return nullptr;
}

int Simulation::detector_inventory_signal(BlockPos position) const {
    const int state=world_.block(position);
    if(simdata::property_value(state,"powered")!="true")return 0;
    const auto* vehicle=detector_vehicle(position);
    return vehicle?inventory_strength(*vehicle):0;
}

void Simulation::update_detector_rail(BlockPos position,bool scheduled) {
    using namespace simdata;
    const int state=world_.block(position);
    if(std::string_view(block(state).name)!="detector_rail")return;
    const bool powered=property_value(state,"powered")=="true";
    if(powered&&!scheduled&&devices_.contains(position))return;
    const bool occupied=detector_vehicle(position)!=nullptr;
    if(powered!=occupied)world_.set(position,with_property(state,"powered",occupied?"true":"false"));
    if(occupied&&devices_.size()<65536) {
        devices_[position]={tick+20,world_.block(position)};
        world_.mark_dirty();
    }
}

void Simulation::tick_vehicles() {
    using namespace simdata;
    for(auto& [id,vehicle]:vehicles) {
        (void)id;
        if(vehicle.portal_cooldown>0) {
            --vehicle.portal_cooldown;
            world_.mark_dirty();
        }
        if(vehicle.type!=25&&vehicle.type!=65)continue;
        BlockPos track{static_cast<int>(std::floor(vehicle.x)),static_cast<int>(std::floor(vehicle.y)),
                       static_cast<int>(std::floor(vehicle.z))};
        if(rail(world_.block({track.x,track.y-1,track.z})))--track.y;
        const int state=world_.block(track);
        if(!rail(state))continue;
        const auto shape=property_value(state,"shape");
        if(shape!="east_west"&&shape!="north_south")continue;
        const bool east_west=shape=="east_west";
        const bool powered=property_value(state,"powered")=="true";
        const std::string_view name=block(state).name;
        const bool accelerator=name=="powered_rail";
        if(name=="activator_rail"&&vehicle.type==65&&vehicle.enabled==powered) {
            vehicle.enabled=!powered;
            world_.mark_dirty();
        }

        double& coordinate=east_west?vehicle.x:vehicle.z;
        double& velocity=east_west?vehicle.vx:vehicle.vz;
        double& lateral=east_west?vehicle.z:vehicle.x;
        double& lateral_velocity=east_west?vehicle.vz:vehicle.vx;
        const auto speed=std::min(2.0,std::hypot(velocity,lateral_velocity));
        velocity=std::copysign(speed,velocity);
        lateral_velocity=0;
        if(accelerator&&!powered)velocity=speed<.03?0:velocity*.5;
        const bool waterlogged=property_value(state,"waterlogged")=="true";
        const double maximum=waterlogged?.2:.4;
        const double next=coordinate+std::clamp(velocity,-maximum,maximum);
        auto destination=track;
        if(east_west)destination.x=static_cast<int>(std::floor(next));
        else destination.z=static_cast<int>(std::floor(next));
        if(World::valid(destination)&&(!world_.solid(destination)||rail(world_.block(destination))))
            coordinate=next;
        else velocity=0;
        lateral=(east_west?track.z:track.x)+.5;
        vehicle.y=track.y+.0625;
        vehicle.vy=0;
        float friction=.98f+(15-inventory_strength(vehicle))*.001f;
        if(waterlogged)friction*=.95f;
        velocity*=friction;
        if(accelerator&&powered) {
            if(std::abs(velocity)>.01)velocity+=std::copysign(.06,velocity);
            else {
                const BlockPos before=east_west?BlockPos{track.x-1,track.y,track.z}:BlockPos{track.x,track.y,track.z-1};
                const BlockPos after=east_west?BlockPos{track.x+1,track.y,track.z}:BlockPos{track.x,track.y,track.z+1};
                if(world_.solid(before)&&!rail(world_.block(before)))velocity=.02;
                else if(world_.solid(after)&&!rail(world_.block(after)))velocity=-.02;
            }
        }
        world_.mark_dirty();
    }
    if(!detector_rails_.empty())
        for(const auto position:detector_rails_)update_detector_rail(position,false);
}
}
