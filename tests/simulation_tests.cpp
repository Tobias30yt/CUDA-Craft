#include "cudacraft/world.hpp"
#include "cudacraft/game.hpp"
#include "cudacraft/simulation.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/admin.hpp"
#include "cudacraft/mob_data.hpp"
#include <chrono>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <thread>
#include <stdexcept>

using namespace cudacraft;
namespace sd=cudacraft::simdata;
int checks=0;
void check(bool yes,const char* message){++checks;if(!yes)throw std::runtime_error(message);}
void water_tests() {
    World w;w.set({0,64,0},86);w.advance_ticks(50);
    for(int x=1;x<=7;++x)check(w.block({x,64,0})==86+x,"water level and seven-block range");
    check(w.block({8,64,0})==0,"water stops at range eight");
    w.set({0,64,0},0);w.advance_ticks(160);
    for(int x=-8;x<=8;++x)for(int z=-8;z<=8;++z)check(w.block({x,64,z})==0,"flow recedes after removing source");
    World falling;falling.set({15,68,0},86);falling.advance_ticks(60);
    check(falling.block({15,67,0})==94,"falling water state");
    check(sd::water(falling.block({16,64,0})),"flow crosses chunk boundary");
    falling.set({15,68,0},0);falling.advance_ticks(200);
    check(falling.block({15,64,0})==0&&falling.block({16,64,0})==0,"falling flow drains");
    World infinite;infinite.set({0,64,0},86);infinite.set({2,64,0},86);infinite.advance_ticks(20);
    check(infinite.block({1,64,0})==86,"two sources regenerate middle source");
    World cool;cool.set({0,64,0},86);cool.set({1,64,0},102);cool.advance_ticks(6);
    check(cool.block({1,64,0})==sd::obsidian_state,"source lava cools to obsidian");
}
void redstone_tests() {
    World w;w.set({0,64,0},sd::lever_first+1);
    for(int x=1;x<=6;++x)w.set({x,64,0},sd::redstone_wire_state);
    w.set({7,64,0},sd::redstone_lamp_state);w.advance_ticks(1);
    check(sd::wire_power(w.block({1,64,0}))==0,"unpowered dust");
    w.simulation().interact({0,64,0});w.advance_ticks(1);
    for(int x=1;x<=6;++x)check(sd::wire_power(w.block({x,64,0}))==16-x,"wire power attenuates");
    check(w.block({7,64,0})==sd::redstone_lamp_first,"lamp powers on");
    w.simulation().interact({0,64,0});w.advance_ticks(1);
    check(sd::wire_power(w.block({6,64,0}))==0,"wire has no self-sustaining power loop");
    check(w.block({7,64,0})==sd::redstone_lamp_first,"lamp off delay");
    w.advance_ticks(4);check(w.block({7,64,0})==sd::redstone_lamp_last,"lamp turns off");
    World diode;diode.set({0,64,0},sd::lever_first+1);
    diode.set({1,64,0},sd::repeater_first+2*4+3); // Facing west: input west, output east.
    diode.set({2,64,0},sd::redstone_lamp_state);diode.advance_ticks(1);
    diode.simulation().interact({0,64,0});diode.advance_ticks(1);
    check((diode.block({1,64,0})-sd::repeater_first)%2==1,"repeater waits two game ticks");
    diode.advance_ticks(2);check((diode.block({1,64,0})-sd::repeater_first)%2==0,"repeater switches on");
    check(diode.block({2,64,0})==sd::redstone_lamp_first,"repeater powers output");
    diode.set({1,64,-1},sd::repeater_first+3);diode.set({1,64,-2},sd::redstone_block_state);diode.advance_ticks(8);
    check((diode.block({1,64,0})-sd::repeater_first)%4<2,"side repeater locks diode");
    diode.simulation().interact({0,64,0});diode.advance_ticks(8);
    check((diode.block({1,64,0})-sd::repeater_first)%2==0,"locked diode retains output after input removed");
    diode.set({1,64,-2},0);diode.advance_ticks(12);
    check(diode.block({2,64,0})==sd::redstone_lamp_last,"unlock propagates input change");
    diode.simulation().interact({0,64,0});diode.advance_ticks(8);
    diode.simulation().interact({0,64,0});diode.advance_ticks(8);
    check(diode.block({2,64,0})==sd::redstone_lamp_last,"repeater output drains");
    World torch;torch.set({0,64,0},1);torch.set({0,65,0},sd::redstone_torch_first);torch.set({1,64,0},sd::lever_first+8+3*2+1);
    torch.advance_ticks(4);check(torch.block({0,65,0})==sd::redstone_torch_first,"unpowered torch stays lit");
    torch.simulation().interact({1,64,0});torch.advance_ticks(4);
    check(torch.block({0,65,0})==sd::redstone_torch_last,"powered support inverts torch");
    torch.simulation().interact({1,64,0});torch.advance_ticks(4);
    check(torch.block({0,65,0})==sd::redstone_torch_first,"torch reactivates");
}
struct Session {
    World world;
    int revision=0;float health=20;int food=20;bool died=false;int respawns=0,spawns=0;
    GameSession player;
    Session():player(world,auth::Profile{},[this](std::span<const std::uint8_t> bytes){
        protocol::Reader r(bytes);int id=r.varint();
        if(id==0x12||id==0x14){(void)r.varint();revision=r.varint();}
        if(id==0x66){health=read_f32(r);food=r.varint();}
        if(id==0x42)died=true;if(id==0x50)++respawns;if(id==0x01)++spawns;
    }){}
    void begin() {player.profile.name="CudaTester";player.start();world.admin().execute("op CudaTester",nullptr,[](const std::string&){});player.handle(Packet(0).var(1).data);std::this_thread::sleep_for(std::chrono::milliseconds(55));player.poll();}
    void command(const char* text){player.handle(Packet(6).str(text).data);}
    void creative(int index,int item,int count){player.handle(Packet(0x37).u16(static_cast<unsigned short>(index)).var(count).var(item).var(0).var(0).data);}
    void click(int index,int button=0,int window=0){player.handle(Packet(0x11).var(window).var(revision).u16(static_cast<unsigned short>(index)).u8(static_cast<unsigned char>(button)).var(0).var(0).u8(0).data);}
    const PlayerState& saved(){return world.simulation().saved_players.at(std::string(32,'0'));}
};
void survival_tests() {
    Session s;s.begin();s.creative(36,sd::item_id("dirt"),3);s.command("gamemode survival");
    check(s.player.survival(),"survival command");
    s.creative(36,sd::item_id("diamond_block"),64);
    check(s.saved().inventory[36].count==3&&s.saved().inventory[36].item==sd::item_id("dirt"),"survival rejects creative inventory injection");
    s.player.handle(Packet(0x3f).var(0).pos({1,63,1}).var(1).f32(.5).f32(1).f32(.5).u8(0).u8(0).var(1).data);
    check(s.world.block({1,64,1})==10,"survival places dirt");
    check(s.saved().inventory[36].count==2,"placement consumes one item");
    auto dig=[&](int action){s.player.handle(Packet(0x28).var(action).pos({1,64,1}).u8(1).var(2).data);};
    dig(0);dig(2);check(s.world.block({1,64,1})==10,"early mining finish rejected");
    s.world.advance_ticks(15);dig(2);check(s.world.block({1,64,1})==0,"timed mining succeeds");
    check(!s.world.simulation().mobs.empty(),"mining spawns item drop");
    s.world.advance_ticks(12);check(s.saved().inventory[36].count==3,"nearby drop picked up");
    s.player.damage(7,"test");check(s.health==13,"survival damage packet");
    s.player.damage(7,"test");check(s.health==13,"damage immunity interval");
    s.world.advance_ticks(11);s.player.damage(20,"test");check(s.died&&!s.player.alive(),"death state and packet");
    for(auto stack:s.saved().inventory)check(!stack.item,"inventory dropped on death");
    s.player.handle(Packet(0x0b).var(0).data);check(s.respawns==1&&s.player.alive()&&s.health==20,"respawn restores player");
    s.player.handle(Packet(0).var(2).data);std::this_thread::sleep_for(std::chrono::milliseconds(55));s.player.poll();
    s.command("gamemode creative");s.creative(36,sd::item_id("oak_log"),2);s.command("gamemode survival");
    s.click(36);s.click(1,1);s.click(36); // One log in 2x2 grid, remainder back to hotbar.
    s.click(0);s.click(37);
    check(s.saved().inventory[37].item==sd::item_id("oak_planks")&&s.saved().inventory[37].count==4,"craft logs into four planks");
    check(s.saved().inventory[36].count==1,"craft consumes exact ingredient count");
    const int pig=s.world.simulation().spawn(100,s.player.x+1,s.player.y,s.player.z);
    s.world.advance_ticks(1);
    s.player.poll();check(s.spawns>0,"mob spawn packet");
    s.command("gamemode creative");s.world.simulation().attack(s.player,pig);
    check(!s.world.simulation().mobs.contains(pig),"mob can be killed");
    bool pork=false;for(auto [id,m]:s.world.simulation().mobs)pork|=m.drop.item==sd::porkchop_item;
    check(pork,"pig drops pork");
}
void persistence_tests() {
    auto path=std::filesystem::temp_directory_path()/std::filesystem::path("cuda-craft-simulation-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    {
        World w(path);w.set({15,67,0},86);w.set({0,64,0},sd::lever_first);w.set({1,64,0},sd::repeater_first+11);w.advance_ticks(1);
        PlayerState p;p.mode=0;p.health=7;p.food=11;p.inventory[36]={sd::item_id("wooden_pickaxe"),1,5};p.cursor={sd::bread_item,2,0};p.crafting[7]={sd::stick_item,3,0};w.simulation().saved_players[std::string(32,'1')]=p;
        w.simulation().spawn(100,5,64,5);w.mark_dirty();w.save();
    }
    {
        World w(path);check(w.simulation().tick==1,"saved simulation clock");check(w.simulation().pending_water()>0,"pending water ticks restored");
        check(w.simulation().saved_players.at(std::string(32,'1')).inventory[36].damage==5,"tool wear persists");
        check(w.simulation().saved_players.at(std::string(32,'1')).cursor.count==2&&w.simulation().saved_players.at(std::string(32,'1')).crafting[7].count==3,"cursor and open crafting grid persist");
        check(w.simulation().mobs.size()==1,"mobs persist");w.advance_ticks(20);
        check(sd::water(w.block({15,64,0})),"pending water resumes");
        check((w.block({1,64,0})-sd::repeater_first)%2==0,"pending repeater resumes");
    }
    // Only remove the individual test-owned save files in the unique directory.
    std::filesystem::remove(path/"blocks.json");std::filesystem::remove(path);
}
void needs_and_mob_tests() {
    Session s;PlayerState initial;initial.mode=0;initial.food=10;initial.saturation=0;initial.x=.5;initial.z=.5;initial.inventory[36]={sd::bread_item,2,0};
    s.world.simulation().saved_players[std::string(32,'0')]=initial;s.begin();
    s.player.handle(Packet(0x40).var(0).var(1).f32(0).f32(0).data);s.world.advance_ticks(31);
    check(s.food==10,"eating waits 32 ticks");s.world.advance_ticks(1);
    check(s.food==15&&s.saved().inventory[36].count==1,"eating restores food and consumes bread");
    s.world.simulation().day_time=13000;const int zombie=s.world.simulation().spawn(150,2,64,.5);
    s.world.advance_ticks(20);check(s.health<20,"zombie approaches and attacks survival player");s.world.simulation().mobs.erase(zombie);
    s.world.set({0,65,0},86);s.world.advance_ticks(340);
    check(s.health<17,"submerged player takes drowning damage");
    Session bucket;bucket.begin();bucket.creative(36,sd::water_bucket_item,1);bucket.command("gamemode survival");
    auto use=[&](BlockPos p){bucket.player.handle(Packet(0x3f).var(0).pos(p).var(1).f32(.5).f32(1).f32(.5).u8(0).u8(0).var(1).data);};
    use({2,63,0});check(bucket.world.block({2,64,0})==86&&bucket.saved().inventory[36].item==sd::bucket_item,"water bucket empties into source");
    use({2,64,0});check(bucket.world.block({2,64,0})==0&&bucket.saved().inventory[36].item==sd::water_bucket_item,"empty bucket collects source");
    Session fall;fall.begin();fall.command("gamemode survival");
    fall.player.handle(Packet(0x1d).f64(.5).f64(72).f64(.5).u8(0).data);
    fall.player.handle(Packet(0x1d).f64(.5).f64(64).f64(.5).u8(1).data);
    check(fall.health==15,"eight block fall deals five damage");
}
void building_tests() {
    Session s;s.begin();
    auto place=[&](BlockPos p,int face,float hit_y=1.f){s.player.handle(Packet(0x3f).var(0).pos(p).var(face).f32(.5f).f32(hit_y).f32(.5f).u8(0).u8(0).var(1).data);};
    s.creative(36,sd::item_id("oak_slab"),3);s.command("gamemode survival");place({2,63,1},1);
    check(sd::property_value(s.world.block({2,64,1}),"type")=="bottom","bottom slab placement through protocol");
    place({2,64,1},1);check(sd::property_value(s.world.block({2,64,1}),"type")=="double"&&s.saved().inventory[36].count==1,"same slab merges and consumes one item");
    s.command("gamemode creative");s.creative(36,sd::item_id("oak_door"),2);place({3,63,1},1);
    check(sd::property_value(s.world.block({3,65,1}),"half")=="upper","door creates upper half through protocol");
    s.player.handle(Packet(0x28).var(0).pos({3,65,1}).u8(1).var(2).data);
    check(s.world.block({3,64,1})==0&&s.world.block({3,65,1})==0,"creative break removes both halves through protocol");
    s.world.set({2,64,1},sd::default_state("oak_slab"));s.creative(36,sd::water_bucket_item,1);s.command("gamemode survival");place({2,64,1},1);
    check(sd::property_value(s.world.block({2,64,1}),"waterlogged")=="true"&&s.saved().inventory[36].item==sd::bucket_item,"bucket fills slab and is consumed");
    place({2,64,1},1);check(sd::property_value(s.world.block({2,64,1}),"waterlogged")=="false"&&s.saved().inventory[36].item==sd::water_bucket_item,"bucket extracts contained source");
}
void storage_crafting_tests() {
    struct Recipe {const char* result;int count;std::array<const char*,9> cells;};
    for(const auto& recipe:std::array{
        Recipe{"chest",1,{"oak_planks","oak_planks","oak_planks","oak_planks","","oak_planks","oak_planks","oak_planks","oak_planks"}},
        Recipe{"furnace",1,{"cobblestone","cobblestone","cobblestone","cobblestone","","cobblestone","cobblestone","cobblestone","cobblestone"}},
        Recipe{"smoker",1,{"","oak_log","","oak_log","furnace","oak_log","","oak_log",""}},
        Recipe{"blast_furnace",1,{"iron_ingot","iron_ingot","iron_ingot","iron_ingot","furnace","iron_ingot","smooth_stone","smooth_stone","smooth_stone"}},
        Recipe{"barrel",1,{"oak_planks","oak_slab","oak_planks","oak_planks","","oak_planks","oak_planks","oak_slab","oak_planks"}},
        Recipe{"oak_slab",6,{"oak_planks","oak_planks","oak_planks","","","","","",""}}}) {
        Session s;s.begin();s.command("clear");s.world.set({2,64,2},sd::crafting_table_state);
        s.player.handle(Packet(0x3f).var(0).pos({2,64,2}).var(1).f32(.5).f32(1).f32(.5).u8(0).u8(0).var(1).data);
        for(int i=0;i<9;++i)if(*recipe.cells[i]){s.creative(36,sd::item_id(recipe.cells[i]),1);s.click(37,0,1);s.click(i+1,0,1);}
        s.click(0,0,1);s.click(38,0,1);
        if(s.saved().inventory[37]!=ItemStack{sd::item_id(recipe.result),recipe.count,0}){std::cerr<<"Recipe "<<recipe.result<<" result "<<s.saved().inventory[37].item<<":"<<s.saved().inventory[37].count<<" cursor "<<s.saved().cursor.item<<":"<<s.saved().cursor.count<<" grid:";for(auto item:s.saved().crafting)std::cerr<<' '<<item.item<<":"<<item.count;std::cerr<<'\n';}
        check(s.saved().inventory[37]==ItemStack{sd::item_id(recipe.result),recipe.count,0},"storage/furnace crafting recipe through menu protocol");
        for(auto item:s.saved().crafting)check(item.item==0,"recipe consumes each ingredient once");
    }
}
void knockback_tests() {
    for(int type:{100,150}) {
        Session s;s.begin();s.command("clear");s.command("gamemode survival");
        auto& sim=s.world.simulation();sim.day_time=13000;
        const int id=sim.spawn(type,s.player.x+1,64,s.player.z);
        auto& mob=sim.mobs.at(id);const double start=mob.x;
        sim.attack(s.player,id);
        check(mob.health==(type==100?9:19),"survival punch damages mob");
        check(mob.vx>.39&&mob.vz==0&&mob.vy==.4,"hit pushes away from attacker and lifts mob");
        const double vx=mob.vx;sim.attack(s.player,id);
        check(mob.vx==vx,"attack cooldown does not add another impulse");
        s.world.advance_ticks(5);
        check(mob.x>start+1&&mob.y>64,"knockback survives AI ticks and moves body");
        const auto saved=sim.save();World restored;restored.simulation().load(saved);
        check(restored.simulation().mobs.at(id).vx==mob.vx&&restored.simulation().mobs.at(id).vy==mob.vy,"mob momentum survives save/reload");
        s.world.advance_ticks(35);
        check(mob.y>=64&&mob.y<64.06,"knockback lands above solid floor");
        check(std::abs(mob.vx)<.01,"knockback decays instead of sliding forever");
    }
    Session wall;wall.begin();wall.command("clear");wall.command("gamemode survival");
    auto& sim=wall.world.simulation();const int id=sim.spawn(100,1.5,64,.5);
    for(int y=64;y<68;++y)wall.world.set({2,y,0},1);
    sim.attack(wall.player,id);wall.world.advance_ticks(5);
    check(sim.mobs.at(id).x<=1.55+1e-6,"knockback body cannot pass through wall");
    Session overlap;overlap.begin();overlap.command("clear");overlap.command("gamemode survival");
    auto& same=overlap.world.simulation();int pig=same.spawn(100,overlap.player.x,64,overlap.player.z);
    same.attack(overlap.player,pig);
    check(std::isfinite(same.mobs.at(pig).vx)&&std::isfinite(same.mobs.at(pig).vz)&&std::hypot(same.mobs.at(pig).vx,same.mobs.at(pig).vz)>.39,"overlapping attacker uses finite facing direction");
    Session ceiling;ceiling.begin();ceiling.command("clear");ceiling.command("gamemode survival");
    auto& low=ceiling.world.simulation();int zombie=low.spawn(150,1.5,64,.5);
    for(int x=1;x<5;++x)ceiling.world.set({x,66,0},1);
    low.attack(ceiling.player,zombie);ceiling.world.advance_ticks(2);
    check(low.mobs.at(zombie).y+1.95<=66,"upward knockback respects ceiling");
}
void expansion_tests() {
    Session s;s.begin();s.command("clear");s.creative(36,sd::item_id("diamond"),7);
    for(const auto& type:mobdata::types) {
        const int id=s.world.simulation().spawn(type.id,2,80,2);
        check(id!=0,"every mob type can spawn");check(s.world.simulation().mobs.at(id).health==type.health,"mob starts with profile health");
    }
    World restored;restored.simulation().load(s.world.simulation().save());
    check(restored.simulation().mobs.size()==std::size(mobdata::types),"every mob type survives persistence");
    s.world.set({1,65,1},1);s.player.change_dimension(1);
    auto& nether=s.player.world();check(nether.dimension_id()==1&&nether.biome(0,64,0)==34,"Nether dimension and biome");
    check(nether.block({0,63,0})!=0&&nether.block({0,64,0})==0,"Nether safe arrival");
    check(s.saved().inventory[36].count==7&&s.saved().dimension==1,"travel preserves inventory and saves dimension");
    nether.set({1,65,1},World::item_state(sd::item_id("glass")));s.player.change_dimension(2);
    check(s.player.world().biome(100,49,0)==56&&s.player.world().block({100,48,0})==sd::obsidian_state,"End biome and arrival platform");
    s.player.change_dimension(0);check(s.world.block({1,65,1})==1&&nether.block({1,65,1})==World::item_state(sd::item_id("glass")),"dimension edits stay isolated");
    check(s.saved().inventory[36].count==7,"round trip preserves inventory");
    World blocks;auto state=[](std::string_view name){for(const auto& b:sd::blocks)if(name==b.name)return b.state;return 0;};
    blocks.set({0,68,0},state("sand"));blocks.advance_ticks(4);check(blocks.block({0,64,0})==state("sand")&&blocks.block({0,68,0})==0,"sand falls and settles without duplication");
    blocks.set({2,64,0},state("blue_concrete_powder"));blocks.set({3,64,0},sd::water_state);blocks.advance_ticks(1);check(blocks.block({2,64,0})==state("blue_concrete"),"powder hardens beside water");
    blocks.set({5,63,0},state("farmland"));blocks.set({5,64,0},state("wheat"));blocks.set({6,63,0},sd::water_state);blocks.advance_ticks(20);
    check(sd::property_value(blocks.block({5,63,0}),"moisture")=="7","farmland hydrates");
    check(blocks.simulation().fertilize({5,64,0}),"bone meal grows crop");
    check(sd::property_value(blocks.block({5,64,0}),"age")!="0","crop age advances");
    blocks.set({8,64,0},state("stone_button"));blocks.simulation().interact({8,64,0});check(sd::property_value(blocks.block({8,64,0}),"powered")=="true","button activates");
    blocks.advance_ticks(20);check(sd::property_value(blocks.block({8,64,0}),"powered")=="false","stone button releases after 20 ticks");
}
void portal_and_dimension_save_tests() {
    Session portal;portal.begin();portal.creative(36,sd::item_id("flint_and_steel"),1);
    for(int x=2;x<=5;++x)for(int y=63;y<=67;++y)if(x==2||x==5||y==63||y==67)portal.world.set({x,y,2},sd::obsidian_state);
    auto light=[&]{portal.player.handle(Packet(0x3f).var(0).pos({3,63,2}).var(1).f32(.5).f32(1).f32(.5).u8(0).u8(0).var(1).data);};
    portal.world.set({4,67,2},0);light();check(std::string_view(sd::block(portal.world.block({3,64,2})).name)!="nether_portal","incomplete frame does not ignite");
    portal.world.set({4,67,2},sd::obsidian_state);light();
    for(int x=3;x<=4;++x)for(int y=64;y<=66;++y)check(std::string_view(sd::block(portal.world.block({x,y,2})).name)=="nether_portal","complete frame fills portal interior");
    portal.player.x=3.5;portal.player.y=64;portal.player.z=2.5;portal.world.advance_ticks(1);
    check(portal.player.world().dimension_id()==1,"standing in portal transfers creative player");
    auto path=std::filesystem::temp_directory_path()/std::filesystem::path("cuda-dimensions-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    int last=0;
    {
        World root(path);root.dimension(1).set({1,65,1},1);root.dimension(2).set({1,70,1},1);
        root.dimension(1).simulation().spawn(mobdata::find("piglin")->id,0,64,0);
        root.dimension(2).advance_ticks(2);
        last=root.dimension(2).simulation().spawn(mobdata::find("warden")->id,100,49,0);
        PlayerState player;player.dimension=2;player.x=100.5;player.y=49;player.inventory[36]={sd::item_id("diamond"),7,0};
        root.simulation().saved_players[std::string(32,'0')]=player;root.mark_dirty();root.save();
    }
    {
        World root(path);check(root.dimension(1).block({1,65,1})==1&&root.dimension(2).block({1,70,1})==1,"both dimension edit files reload");
        check(root.dimension(2).simulation().mobs.at(last).health==500,"high-health mob persists in End");
        check(root.next_entity>last,"loaded dimension entities reserve global identifiers");
        check(root.dimension(2).simulation().tick>=root.dimension(2).simulation().mobs.at(last).born,"loading does not rewind dimension clock behind mob birth");
        const auto& player=root.simulation().saved_players.at(std::string(32,'0'));check(player.dimension==2&&player.inventory[36].count==7,"player dimension and inventory persist");
    }
    for(const auto* name:{"DIM-1","DIM1"}){std::filesystem::remove(path/name/"blocks.json");std::filesystem::remove(path/name);}
    std::filesystem::remove(path/"blocks.json");std::filesystem::remove(path);
}
void portal_invalidation_tests() {
    int portal_state=0;for(const auto& b:sd::blocks)if(std::string_view(b.name)=="nether_portal")portal_state=b.state;
    for(bool along_x:{false,true})for(int damage=0;damage<5;++damage) {
        World w;auto at=[&](int u,int y){return BlockPos{10+(along_x?u:0),64+y,10+(along_x?0:u)};};
        const int portal=sd::with_property(portal_state,"axis",along_x?"x":"z");
        for(int u=-1;u<=2;++u)for(int y=-1;y<=3;++y)w.set(at(u,y),(u==-1||u==2||y==-1||y==3)?sd::obsidian_state:portal);
        w.advance_ticks(1);check(w.block(at(0,0))==portal,"valid portal survives validation in both orientations");
        for(int u:{-1,2})for(int y:{-1,3})w.set(at(u,y),0);
        w.advance_ticks(1);check(w.block(at(0,0))==portal,"optional frame corners do not invalidate portal");
        const BlockPos broken[]={at(0,-1),at(0,3),at(-1,1),at(2,1),at(0,1)};
        w.set(broken[damage],0);w.advance_ticks(1);
        for(int u=0;u<2;++u)for(int y=0;y<3;++y)check(w.block(at(u,y))==0,"damaged frame or interior clears entire portal");
    }
    Session s;s.begin();
    for(int x=2;x<=5;++x)for(int y=63;y<=67;++y)s.world.set({x,y,2},x==2||x==5||y==63||y==67?sd::obsidian_state:sd::with_property(portal_state,"axis","x"));
    s.player.x=3.5;s.player.y=64;s.player.z=2.5;s.world.set({2,65,2},0);s.world.advance_ticks(1);
    check(s.player.world().dimension_id()==0,"invalid portal is cleared before player dimension travel");
}
int main(){try{water_tests();redstone_tests();survival_tests();persistence_tests();needs_and_mob_tests();building_tests();storage_crafting_tests();knockback_tests();expansion_tests();portal_and_dimension_save_tests();portal_invalidation_tests();std::cout<<checks<<" simulation checks passed\n";return 0;}catch(const std::exception& e){std::cerr<<"After "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
