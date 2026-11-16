#include "cudacraft/world.hpp"
#include "cudacraft/simulation.hpp"
#include "cudacraft/simulation_data.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <chrono>
using namespace cudacraft;
namespace sd=cudacraft::simdata;
int checks=0;
void check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
int main(){try{
    for(const auto& b:sd::blocks){
        check(sd::valid_state(b.first)&&sd::valid_state(b.last),"registered range");
        if(b.item)check(World::item_state(b.item)!=0,"every block item has a placement state");
        for(int s=b.first;s<=b.last;++s)for(const auto& p:sd::properties)if(p.first==b.first){
            auto value=sd::property_value(s,p.name);check(!value.empty(),"state property decodes");
            check(sd::with_property(b.state,p.name,value)>=b.first&&sd::with_property(b.state,p.name,value)<=b.last,"property stays in block range");
            check(sd::with_property(s,p.name,value)==s,"property identity round trip");
        }
    }
    World w;
    check(sd::property_value(w.simulation().placement(sd::item_id("oak_log"),5,0),"axis")=="x","logs orient to clicked face");
    check(sd::property_value(w.simulation().placement(sd::item_id("chest"),1,90),"facing")=="east","chest faces player");
    {
        World blocks;
        auto& simulation=blocks.simulation();
        int door=simulation.placement(sd::item_id("oak_door"),1,0);
        check(simulation.place_block({1,64,1},door),"door placement");
        check(sd::property_value(blocks.block({1,64,1}),"half")=="lower"&&sd::property_value(blocks.block({1,65,1}),"half")=="upper","both door halves created");
        check(sd::property_value(blocks.block({1,64,1}),"facing")=="south","door faces placement direction");
        check(simulation.interact({1,65,1}),"upper door half interaction");
        check(sd::property_value(blocks.block({1,64,1}),"open")=="true"&&sd::property_value(blocks.block({1,65,1}),"open")=="true","both door halves open together");
        check(simulation.break_block({1,65,1})&&blocks.block({1,64,1})==0&&blocks.block({1,65,1})==0,"breaking upper door removes pair");
        blocks.set({1,65,1},1);
        check(!simulation.place_block({1,64,1},door)&&blocks.block({1,64,1})==0&&blocks.block({1,65,1})==1,"blocked upper door placement has no partial writes");
        blocks.set({1,318,1},1);check(!simulation.place_block({1,319,1},door)&&blocks.block({1,319,1})==0,"door obeys world ceiling");
        int bed=simulation.placement(sd::item_id("red_bed"),1,0);
        check(simulation.place_block({3,64,3},bed),"bed placement");
        check(sd::property_value(blocks.block({3,64,3}),"part")=="foot"&&sd::property_value(blocks.block({3,64,4}),"part")=="head","bed direction and head");
        check(simulation.break_block({3,64,4})&&blocks.block({3,64,3})==0,"breaking bed head removes foot");
        int fence=sd::default_state("oak_fence");blocks.set({5,64,5},fence);blocks.set({6,64,5},fence);blocks.advance_ticks(1);
        check(sd::property_value(blocks.block({5,64,5}),"east")=="true"&&sd::property_value(blocks.block({6,64,5}),"west")=="true","fences connect both directions");
        blocks.set({6,64,5},0);blocks.advance_ticks(1);check(sd::property_value(blocks.block({5,64,5}),"east")=="false","fence connection removed");
        blocks.set({6,64,5},sd::default_state("nether_brick_fence"));blocks.advance_ticks(1);check(sd::property_value(blocks.block({5,64,5}),"east")=="false","wood and nether fences stay separate");
        int pane=sd::default_state("glass_pane");blocks.set({7,64,7},pane);blocks.set({8,64,7},1);blocks.advance_ticks(1);check(sd::property_value(blocks.block({7,64,7}),"east")=="true","glass pane connects to solid neighbor");
        World wet;int slab=sd::with_property(sd::default_state("oak_slab"),"waterlogged","true");wet.set({1,64,1},slab);wet.advance_ticks(10);
        check(wet.block({1,64,1})==slab&&sd::water(wet.block({2,64,1})),"waterlogged slab emits water without losing block");
        check(wet.simulation().break_block({1,64,1})&&wet.block({1,64,1})==sd::water_state,"breaking waterlogged block preserves source");
    }
    const auto previous=std::filesystem::current_path();
    const auto temporary=std::filesystem::temp_directory_path()/("cuda-template-test-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary/"data/structures/test");std::filesystem::current_path(temporary);
    const auto file=temporary/"data/structures/test/tiny.json";
    {std::ofstream out(file);out<<R"({"format":1,"protocol":774,"size":[2,1,1],"palettes":[[1,29670],[2,29669]],"blocks":[[0,0,0,0],[1,0,0,1]]})";}
    w.place_template("test/tiny",{1,64,1},1);
    check(w.block({1,64,1})==2&&w.block({2,64,1})==29669,"template variant preserves arbitrary states");
    auto count=w.edit_count();
    bool rejected=false;try{w.place_template("test/tiny",{1023,64,0});}catch(...){rejected=true;}
    check(rejected&&w.edit_count()==count,"out of bounds structure is atomic");
    rejected=false;try{w.place_template("../tiny",{0,64,0});}catch(...){rejected=true;}check(rejected,"path traversal rejected");
    rejected=false;try{w.place_template("test/tiny",{0,64,0},2);}catch(...){rejected=true;}check(rejected,"invalid palette rejected");
    std::filesystem::current_path(previous);std::filesystem::remove(file);std::filesystem::remove(file.parent_path());std::filesystem::remove(temporary/"data/structures");std::filesystem::remove(temporary/"data");std::filesystem::remove(temporary);
    std::cout<<checks<<" block registry and structure checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
