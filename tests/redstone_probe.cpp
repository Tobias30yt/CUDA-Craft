#include "cudacraft/simulation_data.hpp"
#include "cudacraft/simulation.hpp"
#include "cudacraft/world.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <tuple>

using namespace cudacraft;
using nlohmann::json;
namespace sd=cudacraft::simdata;

int main(int argc,char** argv) {
    try {
        if(argc>2)throw std::runtime_error("Usage: redstone-probe [world-directory]");
        World world(argc==2?std::filesystem::path(argv[1]):std::filesystem::path{});
        const auto actions=json::parse(std::cin);
        int failures=0;
        std::map<std::string,int> vehicles;
        std::map<std::string,int> explosives;
        std::map<std::string,int> item_probes;
        std::vector<int> resting_items;
        int remote_weight=0;
        for(const auto& action:actions) {
            if(action.contains("next_tick_multiple")) {
                const int multiple=action.at("next_tick_multiple").get<int>();
                if(multiple<=0)throw std::runtime_error("Invalid tick multiple");
                world.advance_ticks(multiple-static_cast<int>(world.simulation().tick%multiple));
                continue;
            }
            if(action.contains("item_probe")) {
                const auto label=action.at("item_probe").get<std::string>();
                const auto point=action.at("pos").get<std::array<double,3>>();
                const auto speed=action.at("velocity").get<std::array<double,3>>();
                if(!action.contains("check")) {
                    const int id=world.simulation().spawn(71,point[0],point[1],point[2],{sd::item_id("stone"),1,0});
                    if(!id||!item_probes.emplace(label,id).second)throw std::runtime_error("Fixture item spawn rejected");
                    auto& item=world.simulation().mobs.at(id);
                    std::tie(item.vx,item.vy,item.vz)=std::tuple{speed[0],speed[1],speed[2]};
                } else {
                    const auto& item=world.simulation().mobs.at(item_probes.at(label));
                    const auto same=[](double a,double b){return std::abs(a-b)<=1e-8;};
                    if(!same(item.x,point[0])||!same(item.y,point[1])||!same(item.z,point[2])||
                       !same(item.vx,speed[0])||!same(item.vy,speed[1])||!same(item.vz,speed[2])) {
                        ++failures;
                        std::cerr<<action.at("check").get<std::string>()<<": unexpected item position or motion\n";
                    }
                }
                continue;
            }
            if(action.contains("enable_chunk_tickets")) {
                world.enable_chunk_tickets();
                for(const auto& entry:action.at("enable_chunk_tickets")) {
                    const auto [cx,cz]=entry.get<std::array<int,2>>();
                    world.force_tick_chunk(cx,cz);
                }
                continue;
            }
            if(action.contains("spawn_resting_item")) {
                const auto [x,y,z]=action.at("pos").get<std::array<double,3>>();
                const int id=world.simulation().spawn(71,x,y,z,{sd::item_id("stone"),1,0});
                if(!id)throw std::runtime_error("Fixture item spawn rejected");
                world.simulation().mobs.at(id).item_on_ground=true;
                resting_items.push_back(id);
                continue;
            }
            if(action.contains("falling_item_count")) {
                const auto observed=std::ranges::count_if(resting_items,[&](int id) {
                    return !world.simulation().mobs.at(id).item_on_ground;
                });
                if(observed!=action.at("falling_item_count").get<int>()) {
                    ++failures;
                    std::cerr<<action.at("check").get<std::string>()<<": found "<<observed<<" falling items\n";
                }
                continue;
            }
            if(action.contains("remote_weight")) {
                if(action.at("remote_weight")=="remove") {
                    if(!world.simulation().mobs.erase(remote_weight))throw std::runtime_error("Remote weight is absent");
                } else {
                    const auto [x,y,z]=action.at("pos").get<std::array<double,3>>();
                    remote_weight=world.simulation().spawn(71,x,y,z,{sd::item_id("stone"),1,0});
                    if(!remote_weight)throw std::runtime_error("Could not drop weight on the original remote input");
                }
                continue;
            }
            if(action.contains("capture")) {
                json states=json::array(),tnt=json::array(),inventories=json::array(),carts=json::array(),items=json::array();
                for(const auto& entry:action.at("capture")) {
                    const auto [x,y,z]=entry.get<std::array<int,3>>();
                    states.push_back(world.block({x,y,z}));
                }
                auto capture_entities=[&](const World& dimension) {
                    for(const auto& [id,charge]:dimension.simulation().primed_tnt) {
                        (void)id;
                        tnt.push_back({{"position",{charge.x,charge.y,charge.z}},
                                       {"velocity",{charge.vx,charge.vy,charge.vz}},{"fuse",charge.fuse},
                                       {"dimension",dimension.dimension_name()}});
                    }
                    for(const auto& [id,vehicle]:dimension.simulation().vehicles) {
                        (void)id;
                        const auto* type=vehicle.type==25?"chest_minecart":vehicle.type==65?"hopper_minecart":"oak_boat";
                        carts.push_back({{"kind",type},{"position",{vehicle.x,vehicle.y,vehicle.z}},
                                         {"velocity",{vehicle.vx,vehicle.vy,vehicle.vz}},
                                         {"portal_cooldown",vehicle.portal_cooldown},
                                         {"dimension",dimension.dimension_name()}});
                    }
                    for(const auto& [id,mob]:dimension.simulation().mobs) {
                        if(mob.type==71)items.push_back({{"dimension",dimension.dimension_name()},
                            {"id",id},{"tick_count",mob.item_ticks},{"on_ground",mob.item_on_ground},
                            {"item",mob.drop.item},{"count",mob.drop.count},{"position",{mob.x,mob.y,mob.z}},
                            {"velocity",{mob.vx,mob.vy,mob.vz}}});
                    }
                };
                capture_entities(world);
                if(action.value("capture_nether",false))capture_entities(world.dimension(1));
                if(action.contains("capture_inventories"))for(const auto& entry:action.at("capture_inventories")) {
                    const auto [x,y,z]=entry.get<std::array<int,3>>();
                    const auto* container=world.simulation().container({x,y,z});
                    if(!container) {
                        inventories.push_back(nullptr);
                        continue;
                    }
                    json stacks=json::array();
                    for(int slot=0;slot<Simulation::container_slots(container->type);++slot) {
                        const auto& stack=container->items[slot];
                        if(stack.item)stacks.push_back({slot,stack.item,stack.count});
                    }
                    const bool hopper=std::string_view(sd::block(container->type).name)=="hopper";
                    inventories.push_back({{"cooldown",hopper?json(container->transfer_cooldown):json(nullptr)},
                                           {"items",std::move(stacks)}});
                }
                std::cout<<json{{"states",std::move(states)},{"tnt",std::move(tnt)},
                                {"inventories",std::move(inventories)},{"vehicles",std::move(carts)},
                                {"items",std::move(items)},{"next_entity",world.next_entity}}.dump()<<'\n';
                continue;
            }
            if(action.contains("tnt")) {
                const auto label=action.at("tnt").get<std::string>();
                if(action.value("remove",false)) {
                    if(!world.simulation().primed_tnt.erase(explosives.at(label)))
                        throw std::runtime_error("Fixture TNT removal did not find the entity");
                    continue;
                }
                const auto point=action.at("pos").get<std::array<double,3>>();
                if(action.contains("check")&&!action.contains("fuse")) {
                    if(world.simulation().primed_tnt.contains(explosives.at(label))) {
                        ++failures;
                        std::cerr<<action.at("check").get<std::string>()<<": TNT still exists\n";
                    }
                    continue;
                }
                const auto speed=action.at("velocity").get<std::array<double,3>>();
                const int fuse=action.at("fuse");
                if(!action.contains("check")) {
                    const int id=world.simulation().spawn_tnt({point[0],point[1],point[2],speed[0],speed[1],speed[2],fuse});
                    if(!id||!explosives.emplace(label,id).second)throw std::runtime_error("Fixture TNT spawn rejected");
                    continue;
                }
                const auto tnt=world.simulation().primed_tnt.find(explosives.at(label));
                const auto same=[](double a,double b){return std::abs(a-b)<1e-8;};
                if(tnt==world.simulation().primed_tnt.end()||tnt->second.fuse!=fuse||
                   !same(tnt->second.x,point[0])||!same(tnt->second.y,point[1])||!same(tnt->second.z,point[2])||
                   !same(tnt->second.vx,speed[0])||!same(tnt->second.vy,speed[1])||!same(tnt->second.vz,speed[2])) {
                    ++failures;
                    std::cerr<<action.at("check").get<std::string>()<<": unexpected TNT position, motion or fuse\n";
                }
                continue;
            }
            if(action.contains("first_item_from")) {
                const auto origin=action.at("pos").get<std::array<double,3>>();
                const auto offset=action.at("first_item_from").get<std::array<double,3>>();
                const auto& mobs=world.simulation().mobs;
                const auto first=std::ranges::find_if(mobs,[&](const auto& entry) {
                    const auto& item=entry.second;
                    return item.type==71&&std::hypot(item.x-origin[0],item.y-origin[1],item.z-origin[2])<=3;
                });
                if(first==mobs.end()||std::abs(first->second.x-origin[0]-offset[0])>.45||
                   std::abs(first->second.y-origin[1]-offset[1])>.45||
                   std::abs(first->second.z-origin[2]-offset[2])>.45) {
                    ++failures;
                    std::cerr<<action.at("check").get<std::string>()<<": unexpected first item\n";
                }
                continue;
            }
            if(action.contains("tnt_present")) {
                const auto point=action.at("pos").get<std::array<double,3>>();
                const bool found=std::ranges::any_of(world.simulation().primed_tnt,[&](const auto& entry) {
                    const auto& charge=entry.second;
                    return std::hypot(charge.x-point[0],charge.y-point[1],charge.z-point[2])<=1.5;
                });
                if(found!=action.at("tnt_present").get<bool>()) {
                    ++failures;
                    std::cerr<<action.at("check").get<std::string>()<<": unexpected primed TNT presence\n";
                }
                continue;
            }
            if(action.contains("vehicle_present")) {
                const auto point=action.at("pos").get<std::array<double,3>>();
                const auto kind=action.at("vehicle_present").get<std::string>();
                const int type=kind=="oak_boat"?89:kind=="hopper_minecart"?65:25;
                const bool found=std::ranges::any_of(world.simulation().vehicles,[&](const auto& entry) {
                    const auto& v=entry.second;
                    return v.type==type&&std::hypot(v.x-point[0],v.y-point[1],v.z-point[2])<=1.5;
                });
                if(found!=action.at("expected").get<bool>()) {
                    ++failures;
                    std::cerr<<action.at("check").get<std::string>()<<": unexpected "<<kind<<" presence\n";
                }
                continue;
            }
            if(action.contains("vehicle_item")) {
                const auto& vehicle=world.simulation().vehicles.at(vehicles.at(action.at("vehicle").get<std::string>()));
                const int expected=sd::item_id(action.at("vehicle_item").get<std::string>());
                if(vehicle.items[0]!=ItemStack{expected,1,0}) {
                    ++failures;
                    std::cerr<<action.at("check").get<std::string>()<<": missing expected minecart stack\n";
                }
                continue;
            }
            if(action.contains("remove_vehicle")) {
                const auto label=action.at("remove_vehicle").get<std::string>();
                world.simulation().vehicles.erase(vehicles.at(label));
                vehicles.erase(label);
                continue;
            }
            if(action.contains("check_saved")) {
                const auto actual=json::parse(world.simulation().save());
                for(const auto& [field,expected]:action.at("check_saved").items()) {
                    if(!actual.contains(field)||actual.at(field)!=expected) {
                        ++failures;
                        std::cerr<<"Loaded simulation field differs: "<<field<<'\n';
                    }
                }
                continue;
            }
            if(action.contains("ticks")) {
                world.advance_ticks(action.at("ticks").get<int>());
                continue;
            }
            if(action.contains("time")) {
                world.simulation().day_time=action.at("time").get<unsigned>();
                continue;
            }
            if(action.contains("vehicle")) {
                const auto label=action.at("vehicle").get<std::string>();
                const auto point=action.at("pos").get<std::array<double,3>>();
                const auto speed=action.at("velocity").get<std::array<double,3>>();
                if(action.contains("check")) {
                    const auto& vehicle=world.simulation().vehicles.at(vehicles.at(label));
                    const std::array actual{vehicle.x,vehicle.y,vehicle.z,vehicle.vx,vehicle.vy,vehicle.vz};
                    const std::array expected{point[0],point[1],point[2],speed[0],speed[1],speed[2]};
                    for(std::size_t i=0;i<actual.size();++i)if(std::abs(actual[i]-expected[i])>1e-8) {
                        ++failures;
                        std::cerr<<action.at("check").get<std::string>()<<": coordinate "<<i
                                 <<" expected "<<expected[i]<<", found "<<actual[i]<<'\n';
                    }
                    continue;
                }
                Vehicle vehicle;
                vehicle.type=action.at("kind")=="hopper_minecart"?65:25;
                vehicle.x=point[0];vehicle.y=point[1];vehicle.z=point[2];
                vehicle.vx=speed[0];vehicle.vy=speed[1];vehicle.vz=speed[2];
                const int id=world.simulation().spawn_vehicle(vehicle);
                if(!id||!vehicles.emplace(label,id).second)throw std::runtime_error("Fixture vehicle spawn rejected");
                continue;
            }
            const auto xyz=action.at("pos").get<std::array<int,3>>();
            const BlockPos pos{xyz[0],xyz[1],xyz[2]};
            if(action.value("interact",false)) {
                if(action.contains("indicator")) {
                    const auto [x,y,z]=action.at("indicator").get<std::array<int,3>>();
                    const int state=world.block({x,y,z});
                    if(!sd::copper_bulb(state))throw std::runtime_error("Fixture lamp is absent");
                    const bool lit=sd::property_value(state,"lit")=="true";
                    if(lit==action.at("lit").get<bool>())continue;
                }
                if(!world.simulation().interact(pos))throw std::runtime_error("Fixture interaction rejected");
                continue;
            }
            if(action.contains("inventory")) {
                const auto& edit=action.at("inventory");
                auto* box=world.simulation().container(pos,true);
                const int slot=edit.at("slot");
                const int count=edit.at("count");
                const auto name=edit.at("item").get<std::string>();
                const int item=count?sd::item_id(name):0;
                if(!box||slot<0||slot>=Simulation::container_slots(box->type)||
                   (count&&(count<0||!item||count>sd::item(item)->stack)))
                    throw std::runtime_error("Invalid fixture inventory edit");
                if(action.contains("check")) {
                    if(box->items[slot]!=ItemStack{item,count,0}) {
                        ++failures;
                        std::cerr<<action.at("check").get<std::string>()<<": expected "<<count<<' '<<name
                                 <<", found count "<<box->items[slot].count<<'\n';
                    }
                    continue;
                }
                box->items[slot]=count?ItemStack{item,count,0}:ItemStack{};
                world.simulation().inventory_signal_changed(pos);
                continue;
            }
            const auto name=action.at("block").get<std::string>();
            const auto properties=action.value("properties",json::object());
            if(action.contains("check")) {
                const int actual=world.block(pos);
                bool matches=name==sd::block(actual).name;
                if(action.contains("state_id"))matches &= actual==action.at("state_id").get<int>();
                for(const auto& [key,value]:properties.items())
                    matches &= sd::property_value(actual,key)==value.get<std::string>();
                if(!matches) {
                    ++failures;
                    std::cerr<<action.at("check").get<std::string>()<<": expected "<<name<<properties.dump()
                             <<", found "<<sd::block(actual).name<<" (state "<<actual<<")";
                    for(const auto& [key,value]:properties.items()) {
                        (void)value;
                        std::cerr<<" "<<key<<"="<<sd::property_value(actual,key);
                    }
                    std::cerr<<'\n';
                }
                continue;
            }
            int state=sd::default_state(name);
            if(name!=sd::block(state).name)throw std::runtime_error("Unknown fixture block: "+name);
            for(const auto& [key,value]:properties.items()) {
                const auto property=value.get<std::string>();
                state=sd::with_property(state,key,property);
                if(sd::property_value(state,key)!=property)throw std::runtime_error("Unknown fixture property: "+key);
            }
            if(!world.set(pos,state))throw std::runtime_error("Fixture block placement rejected");
        }
        return failures?1:0;
    } catch(const std::exception& error) {
        std::cerr<<error.what()<<'\n';
        return 2;
    }
}
