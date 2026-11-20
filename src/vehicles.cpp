#include "cudacraft/simulation.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/world.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

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

double width(const Vehicle& vehicle) { return vehicle.type==89?1.375:static_cast<double>(.98f); }
double height(const Vehicle& vehicle) { return vehicle.type==89?.5625:static_cast<double>(.7f); }

BlockPos cell(double x,double y,double z) {
    return {static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y)),static_cast<int>(std::floor(z))};
}

std::vector<BlockPos> touched_cells(const World& world,const Vehicle& vehicle) {
    const double radius=width(vehicle)/2;
    return world.occupied_cells({vehicle.x-radius,vehicle.y,vehicle.z-radius},
                                {vehicle.x+radius,vehicle.y+height(vehicle),vehicle.z+radius});
}

void tick_boat(const World& world,Vehicle& vehicle) {
    constexpr double radius=1.375/2;
    const auto block=world.block(cell(vehicle.x,vehicle.y+.001,vehicle.z));
    const bool submerged=simdata::water(block)||simdata::property_value(block,"waterlogged")=="true";
    vehicle.vx*=.9;
    vehicle.vz*=.9;
    vehicle.vy=submerged?std::max(0.0,vehicle.vy)*.75:vehicle.vy-.04;
    auto move=[&](int axis,double& position,double& speed) {
        const double actual=world.clip_motion({vehicle.x-radius,vehicle.y,vehicle.z-radius},
                                              {vehicle.x+radius,vehicle.y+.5625,vehicle.z+radius},axis,speed);
        position+=actual;
        if(actual!=speed)speed=0;
    };
    move(1,vehicle.y,vehicle.vy);
    if(std::abs(vehicle.vx)<std::abs(vehicle.vz)) {
        move(2,vehicle.z,vehicle.vz);
        move(0,vehicle.x,vehicle.vx);
    } else {
        move(0,vehicle.x,vehicle.vx);
        move(2,vehicle.z,vehicle.vz);
    }
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
        devices_[position]=device_event(20,world_.block(position));
        world_.mark_dirty();
    }
}

bool Simulation::transfer_vehicle(int id,Vehicle& vehicle,BlockPos entrance) {
    const auto exit=world_.entity_portal_exit(entrance,{vehicle.x,vehicle.y,vehicle.z},width(vehicle),height(vehicle));
    if(!exit)return false;
    auto& destination=*exit->world;
    const auto [x,y,z]=exit->position;
    vehicle.x=x;vehicle.y=y;vehicle.z=z;
    if(exit->rotated) {
        std::tie(vehicle.vx,vehicle.vz)=std::pair{vehicle.vz,-vehicle.vx};
        vehicle.yaw+=90;
    }
    vehicle.portal_cooldown=vehicle.type==89?10:300;
    const double radius=width(vehicle)/2;
    if(vehicle.vy<0&&destination.clip_motion({x-radius,y,z-radius},
                                              {x+radius,y+height(vehicle),z+radius},1,vehicle.vy)==0)vehicle.vy=0;
    auto arrival_vehicle=vehicles.extract(id);
    arrival_vehicle.key()=world_.root().next_entity++;
    destination.simulation().vehicles.insert(std::move(arrival_vehicle));
    world_.mark_dirty();
    destination.mark_dirty();
    return true;
}

void Simulation::collect_vehicle_items(Vehicle& vehicle) {
    if(vehicle.type!=65||!vehicle.enabled)return;
    const auto above=cell(vehicle.x,vehicle.y+1,vehicle.z);
    if(const auto* bank=container(above);bank&&std::string_view(simdata::block(bank->type).name)=="jukebox"&&bank->items[0].item) {
        auto empty=std::find_if(vehicle.items.begin(),vehicle.items.begin()+5,[](const ItemStack& stack){return !stack.item;});
        if(empty!=vehicle.items.begin()+5) {
            const auto record=bank->items[0];
            if(set_jukebox_record(above,{})) {
                *empty=record;
                world_.mark_dirty();
                return;
            }
        }
    }
    for(auto it=mobs.begin();it!=mobs.end();++it) {
        auto& item=it->second;
        if(item.type!=71||std::abs(vehicle.x-item.x)>=.625||std::abs(vehicle.z-item.z)>=.625||
           item.y+.25<=vehicle.y+.5||item.y>=vehicle.y+2)continue;
        const int leftover=store_item(std::span(vehicle.items).first(5),item.drop);
        if(leftover==item.drop.count)continue;
        item.drop.count=leftover;
        if(!leftover)mobs.erase(it);
        world_.mark_dirty();
        return;
    }
}

void Simulation::tick_vehicles() {
    using namespace simdata;
    for(auto current=vehicles.begin();current!=vehicles.end();) {
        auto& [id,vehicle]=*current++;
        const auto world_tick=world_.root().simulation().tick;
        if(vehicle.last_world_tick==world_tick||!world_.ticks_entities(vehicle.x,vehicle.z))continue;
        vehicle.last_world_tick=world_tick;
        const auto contact=std::exchange(vehicle.portal_entry,std::nullopt);
        if(contact&&vehicle.portal_cooldown==0&&transfer_vehicle(id,vehicle,*contact))continue;
        if(vehicle.portal_cooldown>0) {
            --vehicle.portal_cooldown;
            world_.mark_dirty();
        }
        auto update_portal=[&](const std::vector<BlockPos>& occupied) {
            for(auto p:occupied)if(std::string_view(block(world_.block(p)).name)=="nether_portal") {
                if(vehicle.portal_cooldown>0)vehicle.portal_cooldown=vehicle.type==89?10:300;
                else vehicle.portal_entry=p;
                world_.mark_dirty();
                break;
            }
        };
        if(vehicle.type==89) {
            tick_boat(world_,vehicle);
            const auto block=cell(vehicle.x,vehicle.y,vehicle.z);
            const auto occupied=touched_cells(world_,vehicle);
            const bool burning=std::ranges::any_of(occupied,[&](BlockPos p) {
                const std::string_view name=simdata::block(world_.block(p)).name;
                return name=="fire"||name=="soul_fire";
            });
            if(!World::valid(block)||vehicle.y<world_.min_y()-1)vehicles.erase(id);
            else if(burning) {
                const int item=spawn(71,vehicle.x,vehicle.y,vehicle.z,{item_id("oak_boat"),1,0});
                if(item) {
                    auto& drop=mobs.at(item);
                    drop.last_world_tick=world_tick;
                    const auto random=static_cast<std::uint64_t>(item)*0x9e3779b97f4a7c15ULL;
                    drop.vx=(static_cast<double>(random>>48)/65536-.5)*.2;
                    drop.vy=.2;
                    drop.vz=(static_cast<double>((random>>16)&65535)/65536-.5)*.2;
                    vehicles.erase(id);
                }
            } else update_portal(occupied);
            world_.mark_dirty();
            continue;
        }
        BlockPos track{static_cast<int>(std::floor(vehicle.x)),static_cast<int>(std::floor(vehicle.y)),
                       static_cast<int>(std::floor(vehicle.z))};
        if(rail(world_.block({track.x,track.y-1,track.z})))--track.y;
        const int state=world_.block(track);
        if(!rail(state)){update_portal(touched_cells(world_,vehicle));collect_vehicle_items(vehicle);continue;}
        const auto shape=property_value(state,"shape");
        if(shape!="east_west"&&shape!="north_south"){update_portal(touched_cells(world_,vehicle));collect_vehicle_items(vehicle);continue;}
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
        update_portal(touched_cells(world_,vehicle));
        collect_vehicle_items(vehicle);
        world_.mark_dirty();
    }
    if(!detector_rails_.empty())
        for(const auto position:detector_rails_)update_detector_rail(position,false);
}
}
