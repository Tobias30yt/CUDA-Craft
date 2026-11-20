#include "cudacraft/simulation.hpp"
#include "cudacraft/game.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/world.hpp"
#include <algorithm>
#include <cmath>
#include <tuple>
#include <utility>

namespace cudacraft {

int Simulation::store_item(std::span<ItemStack> slots,ItemStack stack) {
    const auto* item=simdata::item(stack.item);
    if(!item||stack.count<=0)return stack.count;
    for(auto& destination:slots) {
        if(destination.item&&(destination.item!=stack.item||destination.damage!=stack.damage))continue;
        const int added=std::min(stack.count,item->stack-destination.count);
        if(added<=0)continue;
        destination={stack.item,destination.count+added,stack.damage};
        stack.count-=added;
        if(!stack.count)break;
    }
    return stack.count;
}

void Simulation::tick_items() {
    constexpr double radius=.125,height=.25;
    const auto world_tick=world_.root().simulation().tick;
    for(auto it=mobs.begin();it!=mobs.end();) {
        auto& [id,item]=*it++;
        if(item.type!=71||item.last_world_tick==world_tick||!world_.ticks_entities(item.x,item.z))continue;
        item.last_world_tick=world_tick;
        ++item.item_ticks;
        const int age=++item.item_age;
        if(age>=6000) {
            mobs.erase(id);
            world_.mark_dirty();
            continue;
        }
        const auto entrance=std::exchange(item.portal_entry,std::nullopt);
        if(entrance&&item.portal_cooldown==0) {
            const auto exit=world_.entity_portal_exit(*entrance,{item.x,item.y,item.z},radius*2,height);
            if(exit) {
                const auto [x,y,z]=exit->position;
                item.x=x;item.y=y;item.z=z;
                if(exit->rotated)std::tie(item.vx,item.vz)=std::pair{item.vz,-item.vx};
                item.portal_cooldown=300;
                auto& arrival=exit->world->simulation();
                item.born=std::min(item.born,arrival.tick);
                auto arrival_item=mobs.extract(id);
                arrival_item.key()=arrival_item.mapped().id=world_.root().next_entity++;
                arrival_item.mapped().item_ticks=0;
                arrival.mobs.insert(std::move(arrival_item));
                world_.mark_dirty();
                exit->world->mark_dirty();
                continue;
            }
        }
        if(item.portal_cooldown>0)--item.portal_cooldown;
        const BlockPos cell{static_cast<int>(std::floor(item.x)),static_cast<int>(std::floor(item.y+.1)),
                            static_cast<int>(std::floor(item.z))};
        const int state=world_.block(cell);
        const bool wet=simdata::water(state)||simdata::property_value(state,"waterlogged")=="true";
        if(wet) {
            item.vx*=.99f;item.vz*=.99f;
            if(item.vy<.06f)item.vy+=.0005f;
        } else item.vy-=.04;
        auto move=[&](int axis,double& position,double speed) {
            const auto actual=world_.clip_motion({item.x-radius,item.y,item.z-radius},
                                                {item.x+radius,item.y+height,item.z+radius},axis,speed);
            position+=actual;
            return actual!=speed;
        };
        // Vanilla staggers nearly stationary ground items across four server ticks.
        // This also determines how quickly one starts falling when its support opens.
        if(!item.item_on_ground||item.vx*item.vx+item.vz*item.vz>1.e-5f||(item.item_ticks+id)%4==0) {
            item.item_on_ground=move(1,item.y,item.vy)&&item.vy<0;
            if(std::abs(item.vx)<std::abs(item.vz)) {
                if(move(2,item.z,item.vz))item.vz=0;
                if(move(0,item.x,item.vx))item.vx=0;
            } else {
                if(move(0,item.x,item.vx))item.vx=0;
                if(move(2,item.z,item.vz))item.vz=0;
            }
            float slipperiness=1.f;
            if(item.item_on_ground) {
                const BlockPos support{static_cast<int>(std::floor(item.x)),static_cast<int>(std::floor(item.y-1.e-7)),
                                       static_cast<int>(std::floor(item.z))};
                const std::string_view material=simdata::block(world_.block(support)).name;
                slipperiness=.6f;
                if(material=="slime_block")slipperiness=.8f;
                else if(material=="blue_ice")slipperiness=.989f;
                else if(material=="ice"||material=="packed_ice"||material=="frosted_ice")slipperiness=.98f;
                item.vy=material=="slime_block"?-.8*item.vy:0;
            }
            const double drag=slipperiness*.98f;
            item.vx*=drag;item.vy*=.98;item.vz*=drag;
        }
        for(auto p:world_.occupied_cells({item.x-radius,item.y,item.z-radius},
                                        {item.x+radius,item.y+height,item.z+radius})) {
            if(std::string_view(simdata::block(world_.block(p)).name)!="nether_portal")continue;
            if(item.portal_cooldown>0)item.portal_cooldown=300;
            else item.portal_entry=p;
            break;
        }
        if(age>=10)for(auto* player:world_.players) {
            const auto dx=player->x-item.x,dy=player->y-item.y,dz=player->z-item.z;
            if(dx*dx+dy*dy+dz*dz>3||!player->alive())continue;
            const int left=player->add_item(item.drop);
            if(left!=item.drop.count)for(auto* observer:world_.players)
                observer->collect(id,player->entity_id,item.drop.count-left);
            item.drop.count=left;
            if(!left)break;
        }
        if(!item.drop.count||item.y<world_.min_y()-1||std::abs(item.x)>=1024||std::abs(item.z)>=1024)
            mobs.erase(id);
        world_.mark_dirty();
    }
}

}
