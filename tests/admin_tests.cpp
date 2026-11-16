#include "cudacraft/admin.hpp"
#include "cudacraft/game.hpp"
#include "cudacraft/performance.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace cudacraft;
int checks=0;
void check(bool valid,const char* reason){++checks;if(!valid)throw std::runtime_error(reason);}
int main(){try{
    auto start=Performance::Clock::time_point{};Performance perf(start);
    for(int i=1;i<=100;++i)perf.tick(i==100?100:10,start+std::chrono::milliseconds(i*50));
    auto now=start+std::chrono::seconds(5);auto times=perf.ticks(now);
    check(perf.tps(1,now)==20&&perf.tps(60,now)==20,"TPS uses wall time");
    check(times.count==100&&times.p95==10&&times.maximum==100&&times.mean==10.9,"mean/p95/max computation");
    check(perf.over_budget==1,"over-budget ticks counted");
    check(perf.tps(1,now+std::chrono::seconds(2))==0,"stalled server reports zero recent TPS");
    check(perf.ticks(now+std::chrono::seconds(61)).count==0,"old timings expire");
    auto directory=std::filesystem::temp_directory_path()/("cuda-craft-admin-"+std::to_string(Performance::Clock::now().time_since_epoch().count()));
    auth::Profile identity;identity.name="AdminTester";identity.uuid[0]=42;
    {
        World world(directory);std::vector<std::string> replies;auto reply=[&](const std::string& text){replies.push_back(text);};
        auto run=[&](std::string command,GameSession* sender=nullptr){replies.clear();world.admin().execute(std::move(command),sender,reply);};
        int trees=0;GameSession player(world,identity,[&](std::span<const std::uint8_t> packet){protocol::Reader r(packet);if(r.varint()==0x10)++trees;});player.start();
        check(!world.admin().is_operator(identity),"first player is not automatically OP");
        const auto mode=player.survival();run("gamemode survival",&player);check(replies[0].starts_with("Permission denied")&&player.survival()==mode,"unauthorized mutation denied");
        run("op AdminTester",&player);check(!world.admin().is_operator(identity),"self-op denied");
        run("op Missing");check(replies[0].starts_with("Error:"),"unknown names cannot gain pending OP");
        run("op AdminTester");check(world.admin().is_operator(identity)&&trees==2,"console grants UUID-based OP and refreshes tree");
        run("gamemode survival",&player);check(player.survival(),"OP command executes");
        run("give @s diamond 3",&player);check(replies[0].starts_with("Gave 3"),"give command supplies bounded inventory items");
        run("tp @s 2 70 -3",&player);check(player.x==2&&player.y==70&&player.z==-3,"targeted teleport");
        run("tp ~1 ~ ~-2",&player);check(player.x==3&&player.z==-5,"relative coordinates");
        run("tp NaN 70 0",&player);check(replies[0].starts_with("Error:")&&player.x==3,"NaN coordinate rejected");
        run("tp 999999 70 0",&player);check(replies[0].starts_with("Error:")&&player.x==3,"out-of-border teleport rejected");
        run("give @s diamond 999999",&player);check(replies[0].starts_with("Error:"),"oversized give rejected");
        run("time set night",&player);check(world.simulation().day_time==13000,"time command");
        run("summon zombie 4 64 4",&player);check(world.simulation().mobs.size()==1,"console-compatible summon");
        run("kill @s",&player);check(!player.alive(),"operator kill reaches death workflow");
        run("heal @s",&player);check(replies[0].starts_with("Error:")&&!player.alive(),"heal does not silently bypass respawn");
        run("perf export",&player);auto stats=nlohmann::json::parse(std::ifstream(directory/"performance.json"));
        check(stats.at("simulation_backend")=="CPU"&&stats.at("tps").size()==3,"performance export schema");
        run("save-all");check(std::filesystem::exists(directory/"blocks.json"),"explicit save");
        run("deop AdminTester");check(!world.admin().is_operator(identity)&&trees==3,"deop immediately refreshes permissions");
        run("stop",&player);check(!world.admin().stopping&&replies[0].starts_with("Permission denied"),"deopped user cannot stop");
        run("op AdminTester");
    }
    {
        World world(directory);check(world.admin().is_operator(identity),"OP persists across restart");
        auto impostor=identity;impostor.uuid[0]=43;check(!world.admin().is_operator(impostor),"same name with different UUID is not OP");
        auto renamed=identity;renamed.name="RenamedTester";world.admin().joined(renamed);check(world.admin().is_operator(renamed),"name change preserves UUID rights");
        world.admin().execute("deop RenamedTester",nullptr,[](const std::string&){});check(!world.admin().is_operator(identity),"offline deop by known current name");
        world.admin().execute("stop",nullptr,[](const std::string&){});check(world.admin().stopping,"console stop request");
    }
    {World world(directory);check(!world.admin().is_operator(identity),"deop persists across restart");}
    for(const char* file:{"blocks.json","operators.json","performance.json"})std::filesystem::remove(directory/file);std::filesystem::remove(directory);
    std::cout<<checks<<" admin/performance checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"After "<<checks<<" checks: "<<e.what()<<'\n';return 1;}}
