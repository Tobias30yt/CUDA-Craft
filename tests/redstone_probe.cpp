#include "cudacraft/simulation_data.hpp"
#include "cudacraft/simulation.hpp"
#include "cudacraft/world.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <iostream>

using namespace cudacraft;
using nlohmann::json;
namespace sd=cudacraft::simdata;

int main(int argc,char** argv) {
    try {
        if(argc>2)throw std::runtime_error("Usage: redstone-probe [world-directory]");
        World world(argc==2?std::filesystem::path(argv[1]):std::filesystem::path{});
        const auto actions=json::parse(std::cin);
        int failures=0;
        for(const auto& action:actions) {
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
            const auto xyz=action.at("pos").get<std::array<int,3>>();
            const BlockPos pos{xyz[0],xyz[1],xyz[2]};
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
