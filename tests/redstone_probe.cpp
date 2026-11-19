#include "cudacraft/simulation_data.hpp"
#include "cudacraft/world.hpp"
#include <nlohmann/json.hpp>
#include <array>
#include <iostream>

using namespace cudacraft;
using nlohmann::json;
namespace sd=cudacraft::simdata;

int main() {
    try {
        World world;
        const auto actions=json::parse(std::cin);
        int failures=0;
        for(const auto& action:actions) {
            if(action.contains("ticks")) {
                world.advance_ticks(action.at("ticks").get<int>());
                continue;
            }
            const auto xyz=action.at("pos").get<std::array<int,3>>();
            const BlockPos pos{xyz[0],xyz[1],xyz[2]};
            const auto name=action.at("block").get<std::string>();
            const auto properties=action.value("properties",json::object());
            if(action.contains("check")) {
                const int actual=world.block(pos);
                bool matches=name==sd::block(actual).name;
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
