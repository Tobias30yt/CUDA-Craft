#include "cudacraft/world.hpp"
#include "cudacraft/game.hpp"
#include "cudacraft/simulation.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/smelting_data.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <chrono>
#include <iostream>
using namespace cudacraft;
namespace sd=cudacraft::simdata;
int checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
int main(){try{
    World w;auto& sim=w.simulation();const int chest=sd::default_state("chest");
    check(sim.place_block({2,64,2},chest),"single chest placement");
    check(sim.container_positions({2,64,2}).size()==1,"single chest layout");
    sim.container({2,64,2},true)->items[0]={sd::item_id("diamond"),13,0};
    check(sim.place_block({3,64,2},chest),"second chest placement");
    auto pair=sim.container_positions({2,64,2});check(pair.size()==2&&pair==sim.container_positions({3,64,2}),"both halves share canonical layout");
    check(sd::property_value(w.block(pair[0]),"type")=="left"&&sd::property_value(w.block(pair[1]),"type")=="right","double chest left/right states");
    sim.container({3,64,2},true)->items[2]={sd::item_id("iron_pickaxe"),1,12};
    check(sim.break_block({3,64,2}),"break double chest half");
    check(sim.container_positions({2,64,2}).size()==1&&sd::property_value(w.block({2,64,2}),"type")=="single","remaining half returns to single");
    check(sim.container({2,64,2})->items[0].count==13,"unbroken half retains contents");
    check(sim.mobs.size()==1&&sim.mobs.begin()->second.drop.damage==12,"broken half drops its exact contents");
    sim.container({2,64,2})->items[1]={1,64,0};
    sim.mobs.clear();for(int i=0;i<4095;++i)sim.spawn(71,20,64,20,{1,1,0});
    check(!sim.break_block({2,64,2})&&sim.container({2,64,2})->items[0].count==13,"entity capacity prevents destructive item loss");
    sim.mobs.clear();check(sim.break_block({2,64,2})&&sim.mobs.size()==2,"contents drop once after capacity becomes available");
    check(!sim.container({2,64,2}),"removed container cannot be reopened");
    {
        World furnace;auto& s=furnace.simulation();BlockPos p{2,64,2};
        const char* names[]={"","furnace","blast_furnace","smoker"};
        for(const auto& recipe:smelting::recipes) {
            s.containers.clear();furnace.set(p,sd::default_state(names[recipe.kind]));auto* box=s.container(p,true);
            box->items[0]={recipe.input,1,0};box->items[1]={sd::item_id("coal"),1,0};
            furnace.advance_ticks(recipe.ticks-1);check(box->items[2].item==0,"recipe waits full cooking time");
            furnace.advance_ticks(1);check(box->items[2]==ItemStack{recipe.output,recipe.count,0}&&box->items[0].item==0,"official smelting/blasting/smoking recipe result");
            check(box->burn_left==(recipe.kind==1?1600:800)-recipe.ticks+1,"device-specific fuel burn duration");
        }
        s.containers.clear();furnace.set(p,sd::default_state("furnace"));auto* box=s.container(p,true);
        box->items[0]={sd::item_id("raw_iron"),1,0};box->items[1]={sd::item_id("coal"),2,0};box->items[2]={sd::item_id("iron_ingot"),64,0};
        furnace.advance_ticks(10);check(box->items[1].count==2&&box->burn_left==0,"blocked output does not consume fuel");
        box->items[2]={};furnace.advance_ticks(20);check(box->cook_progress==20,"cooking progresses");
        box->items[0]={sd::item_id("raw_gold"),1,0};furnace.advance_ticks(1);check(box->cook_progress==1,"changed input resets progress");
        box->burn_left=0;box->items[1]={};furnace.advance_ticks(1);check(box->cook_progress==0,"unfuelled progress cools down");
        box->items[0]={sd::item_id("wet_sponge"),1,0};box->items[1]={sd::item_id("lava_bucket"),1,0};furnace.advance_ticks(200);
        check(box->items[2].item==sd::item_id("sponge")&&box->items[1].item==sd::water_bucket_item,"lava bucket remainder and sponge water bucket");
    }
    auto path=std::filesystem::temp_directory_path()/("cuda-container-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        World saved(path);auto& s=saved.simulation();saved.set({2,64,2},sd::default_state("barrel"));
        s.container({2,64,2},true)->items[26]={sd::item_id("iron_pickaxe"),1,41};saved.mark_dirty();saved.save();
        saved.set({3,64,2},sd::default_state("furnace"));auto* oven=s.container({3,64,2},true);oven->items[0]={sd::item_id("raw_iron"),1,0};oven->items[1]={sd::item_id("coal"),1,0};saved.advance_ticks(47);saved.save();
    }
    {
        World restored(path);auto& s=restored.simulation();auto* storage=s.container({2,64,2});
        check(storage&&storage->items[26].damage==41,"container contents and durability survive restart");
        auto* oven=s.container({3,64,2});check(oven&&oven->cook_progress==47&&oven->burn_left==1554,"furnace timer survives restart");
        restored.advance_ticks(153);check(oven->items[2].item==sd::item_id("iron_ingot"),"saved smelting resumes exactly");
        auto payload=nlohmann::json::parse(s.save());payload["containers"].push_back(payload["containers"][0]);
        bool rejected=false;try{Simulation invalid(restored);invalid.load(payload.dump());}catch(...){rejected=true;}check(rejected,"duplicate container positions rejected");
        payload=nlohmann::json::parse(s.save());payload["containers"][0]["items"][26][1]=65;
        rejected=false;try{Simulation invalid(restored);invalid.load(payload.dump());}catch(...){rejected=true;}check(rejected,"invalid persisted stacks rejected");
        payload=nlohmann::json::parse(s.save());payload.erase("containers");Simulation legacy(restored);legacy.load(payload.dump());check(legacy.containers.empty(),"old worlds without container field still load");
    }
    std::filesystem::remove(path/"blocks.json");std::filesystem::remove(path/"native-world.lock");std::filesystem::remove(path);
    std::cout<<checks<<" container persistence and lifecycle checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<" after "<<checks<<" checks\n";return 1;}}
