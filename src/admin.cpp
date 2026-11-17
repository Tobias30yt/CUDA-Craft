#include "cudacraft/admin.hpp"
#include "cudacraft/game.hpp"
#include "cudacraft/gpu.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/mob_data.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif
namespace cudacraft {
namespace {
struct Command {const char* name;const char* usage;bool op;};
constexpr Command definitions[]={
    {"help","help [command]",false},{"version","version",false},{"list","list",false},{"spawn","spawn",false},
    {"perf","perf [export]",false},{"tps","tps",false},{"monitor","monitor on|off [seconds 1..60]",false},
    {"whoami","whoami",false},{"op","op <online or previously seen player>",true},
    {"deop","deop <player>",true},{"ops","ops",true},{"seed","seed",true},
    {"gamemode","gamemode survival|creative [player]",true},{"time","time set day|night|0..23999",true},
    {"summon","summon <mob> [x y z]",true},{"tp","tp <player> | tp x y z | tp <player> <target> | tp <player> x y z",true},
    {"give","give <player> <item> [count 1..64]",true},{"heal","heal [player]",true},{"feed","feed [player]",true},
    {"place","place template <id> <x> <y> <z> [palette variant]",true},
    {"dimension","dimension overworld|nether|end [player]",true},
    {"clear","clear [player]",true},{"kill","kill [player]",true},{"kick","kick <player> [reason]",true},
    {"say","say <message>",true},{"save-all","save-all",true},{"pause","pause on|off (private local session only)",true},{"stop","stop",true}};
std::string lower(std::string s){for(char& c:s)if(c>='A'&&c<='Z')c=static_cast<char>(c-'A'+'a');return s;}
std::string uuid(const auth::Profile& p){std::string s;for(auto b:p.uuid){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
bool valid_name(std::string_view s){return !s.empty()&&s.size()<=16&&s.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_")==s.npos;}
bool valid_uuid(std::string_view s){return s.size()==32&&s.find_first_not_of("0123456789abcdef")==s.npos;}
void write_atomic(const std::filesystem::path& file,std::string_view content) {
    std::filesystem::create_directories(file.parent_path());auto temporary=file;temporary+=".tmp";
    {std::ofstream stream(temporary,std::ios::binary|std::ios::trunc);stream<<content;stream.flush();if(!stream)throw std::runtime_error("Cannot write "+file.filename().string());}
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),file.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace "+file.filename().string());
#else
    std::filesystem::rename(temporary,file);
#endif
}
int integer(std::string_view s,int low,int high){int n=0;auto result=std::from_chars(s.data(),s.data()+s.size(),n);if(result.ec!=std::errc{}||result.ptr!=s.data()+s.size()||n<low||n>high)throw std::runtime_error("Expected integer "+std::to_string(low)+".."+std::to_string(high));return n;}
double coordinate(std::string s,double relative){bool offset=s.starts_with('~');if(offset)s.erase(0,1);if(s.empty()&&offset)return relative;double n=0;auto result=std::from_chars(s.data(),s.data()+s.size(),n);if(result.ec!=std::errc{}||result.ptr!=s.data()+s.size()||!std::isfinite(n))throw std::runtime_error("Invalid coordinate");return offset?relative+n:n;}
std::string joined_words(const std::vector<std::string>& words,std::size_t start){std::string out;for(std::size_t i=start;i<words.size();++i){if(i!=start)out+=' ';out+=words[i];}return out;}
}
Administration::Administration(World& world,std::filesystem::path directory):world_(world),directory_(std::move(directory)) {
    if(directory_.empty()||!std::filesystem::exists(directory_/"operators.json"))return;
    const auto file=directory_/"operators.json";if(std::filesystem::file_size(file)>1024*1024)throw std::runtime_error("Operator file too large");
    std::ifstream stream(file);auto j=nlohmann::json::parse(stream);
    if(j.at("format")!=1)throw std::runtime_error("Unsupported operator file");
    auto load=[](const nlohmann::json& data,std::map<std::string,std::string>& map){
        if(!data.is_object()||data.size()>1024)throw std::runtime_error("Invalid operator registry");
        for(const auto& [id,n]:data.items()){auto name=n.get<std::string>();if(!valid_uuid(id)||!valid_name(name))throw std::runtime_error("Invalid operator identity");map.emplace(id,name);}
    };
    load(j.at("operators"),operators_);load(j.at("known_players"),known_);
}
void Administration::persist() const {if(!directory_.empty())write_atomic(directory_/"operators.json",nlohmann::json{{"format",1},{"operators",operators_},{"known_players",known_}}.dump(2));}
void Administration::joined(const auth::Profile& profile) {
    if(!valid_name(profile.name))return;
    const auto id=uuid(profile);if(known_.contains(id)&&known_.at(id)==profile.name)return;
    if(!known_.contains(id)&&known_.size()>=1024)throw std::runtime_error("Known-player capacity reached");
    const auto previous_known=known_,previous_operators=operators_;
    known_[id]=profile.name;if(operators_.contains(id))operators_[id]=profile.name;
    try{persist();}catch(...){known_=previous_known;operators_=previous_operators;throw;}
}
bool Administration::is_operator(const auth::Profile& profile) const{return operators_.contains(uuid(profile));}
GameSession* Administration::player(std::string_view name,GameSession* sender) const {
    if(name=="@s"){if(sender)return sender;throw std::runtime_error("Console must specify a player");}
    for(auto* p:world_.all_players())if(!p->closing()&&lower(p->profile.name)==lower(std::string(name)))return p;
    throw std::runtime_error("Player is not online: "+std::string(name));
}
std::vector<std::string> Administration::commands(GameSession* sender) const {
    std::vector<std::string> result;for(auto d:definitions)if(!d.op||!sender||is_operator(sender->profile))result.emplace_back(d.name);return result;
}
std::vector<std::string> Administration::suggestions(std::string_view prefix,GameSession* sender) const {
    auto candidates=commands(sender);const bool op=!sender||is_operator(sender->profile);
    for(const char* s:{"monitor on","monitor off"})candidates.emplace_back(s);
    if(op) {
        for(const char* s:{"gamemode survival","gamemode creative","summon pig","summon zombie","time set day","time set night","perf export"})candidates.emplace_back(s);
        for(const auto& [id,name]:known_)for(const char* command:{"op ","deop "})candidates.push_back(std::string(command)+name);
        for(auto* p:world_.all_players())for(const char* command:{"tp ","give ","kick ","heal ","feed ","clear ","kill ","gamemode survival ","gamemode creative "})candidates.push_back(std::string(command)+p->profile.name);
        if(prefix.starts_with("give ")){auto end=prefix.find(' ',5);if(end!=prefix.npos){auto stem=std::string(prefix.substr(0,end+1));for(const auto& item:simdata::items)if(item.id)candidates.push_back(stem+item.name);}}
    }
    std::vector<std::string> result;for(auto& s:candidates)if(s.starts_with(prefix)&&result.size()<32)result.push_back(std::move(s));return result;
}
std::string Administration::performance_json() const {
    const auto& p=world_.performance;auto tick=p.ticks(),work=p.work_times();
    nlohmann::json j={{"version",1},{"uptime_seconds",p.uptime()},{"tps",{p.tps(1),p.tps(10),p.tps(60)}},
        {"simulation_ms",{{"samples",tick.count},{"average",tick.mean},{"p95",tick.p95},{"max",tick.maximum}}},
        {"loop_work_ms",{{"samples",work.count},{"average",work.mean},{"p95",work.p95},{"max",work.maximum}}},
        {"process_cpu_percent",p.cpu_available?nlohmann::json(p.cpu_percent):nlohmann::json(nullptr)},
        {"working_set_bytes",p.process_available?nlohmann::json(p.working_set):nlohmann::json(nullptr)},
        {"private_commit_bytes",p.process_available?nlohmann::json(p.private_bytes):nlohmann::json(nullptr)},
        {"ticks",p.tick_count},{"simulation_ticks_over_50ms",p.over_budget},{"dropped_tick_slots",p.dropped_tick_slots},
        {"network",{{"received_bytes",p.bytes_received},{"sent_bytes",p.bytes_sent},{"receive_bytes_per_second",p.receive_rate},{"send_bytes_per_second",p.send_rate},{"queued_bytes",p.queued_bytes},{"connections",p.connections}}},
        {"players",world_.all_players().size()},{"entities",world_.simulation().mobs.size()},{"water_queue",world_.simulation().pending_water()},
        {"cached_chunks",world_.cached_chunks()},{"edits",world_.edit_count()},
        {"chunks",{{"generated",p.chunks_generated},{"sent",p.chunks_sent},{"cache_hits",p.cache_hits},{"last_generation_ms",p.last_generation_ms},{"max_generation_ms",p.max_generation_ms},{"last_packet_ms",p.last_chunk_ms}}},
        {"saves",p.saves},{"last_save_ms",p.last_save_ms},{"cuda_compiled",gpu::compiled()},
        {"generator",world_.options().generator},{"simulation_backend","CPU"},{"paused",world_.paused},{"local_session",world_.local_session}};
    return j.dump(2);
}
void Administration::poll() {
    world_.performance.sample_process();const auto now=std::chrono::steady_clock::now();
    if(console_monitor_&&now-last_console_>=std::chrono::seconds(monitor_seconds_)) {
        std::cout<<"[MONITOR] "<<world_.performance.brief()<<" | players "<<world_.all_players().size()<<" | entities "<<world_.simulation().mobs.size()<<" | water "<<world_.simulation().pending_water()<<std::endl;last_console_=now;
    }
    if(now-last_hud_>=std::chrono::seconds(1)) {
        for(auto* p:world_.all_players())if(p->monitor_&&!p->closing())p->send(Packet(0x77).text(world_.performance.brief()).u8(1));last_hud_=now;
    }
}
void Administration::execute(std::string command,GameSession* sender,Reply reply) {
    if(command.starts_with('/'))command.erase(0,1);
    if(command.empty())return;
    if(command.size()>2048||command.find_first_of("\r\n\x1b")!=command.npos){reply("Error: invalid command text");return;}
    std::istringstream input(command);std::vector<std::string> a;std::string word;while(input>>word)a.push_back(word);if(a.empty())return;
    a[0]=lower(a[0]);if(a[0].starts_with("minecraft:"))a[0].erase(0,10);
    auto definition=std::find_if(std::begin(definitions),std::end(definitions),[&](auto d){return a[0]==d.name;});
    if(definition==std::end(definitions)){reply("Unknown command. Use /help.");return;}
    const bool op=!sender||is_operator(sender->profile);
    if(definition->op&&!op){reply("Permission denied: operator required. The console can grant op <player>.");return;}
    auto usage=[&]{throw std::runtime_error(std::string("Usage: ")+definition->usage);};
    auto arity=[&](std::size_t low,std::size_t high){if(a.size()<low||a.size()>high)usage();};
    try {
        const auto& cmd=a[0];
        if(cmd=="help") {
            arity(1,2);if(a.size()==2){for(auto d:definitions)if(a[1]==d.name&&(!d.op||op)){reply(d.usage);return;}reply("Unknown or unavailable command.");}
            else {std::string line="Commands:";for(const auto& c:commands(sender)){if(line.size()+c.size()>180){reply(line);line="";}line+=' '+c;}reply(line);reply("help <command> shows syntax. @s means yourself; coordinates accept ~ offsets.");}
        } else if(cmd=="version"){arity(1,1);reply("CUDA Craft 0.11.0 | Minecraft Java 1.21.11 | protocol 774");}
        else if(cmd=="whoami"){arity(1,1);reply(sender?sender->profile.name+" | UUID "+uuid(sender->profile)+(op?" | operator":" | player"):"Server console | full permissions");}
        else if(cmd=="list"){arity(1,1);std::string list="Online ("+std::to_string(world_.all_players().size())+"):";for(auto* p:world_.all_players())list+=' '+p->profile.name;reply(list);}
        else if(cmd=="seed"){arity(1,1);reply("Seed: "+std::to_string(world_.options().seed)+" | generator: "+world_.options().generator);}
        else if(cmd=="spawn"){arity(1,1);if(!sender)throw std::runtime_error("Use tp <player> x y z from console");if(!sender->alive())throw std::runtime_error("Respawn first");sender->teleport_home();reply("Teleported to spawn.");}
        else if(cmd=="perf"||cmd=="tps") {
            arity(1,cmd=="perf"?2:1);
            if(a.size()==2){if(a[1]!="export")usage();if(!op)throw std::runtime_error("Permission denied: operator required for export");if(directory_.empty())throw std::runtime_error("This world has no save directory");write_atomic(directory_/"performance.json",performance_json());reply("Exported "+(directory_/"performance.json").string());return;}
            reply(world_.performance.brief());if(cmd=="tps")return;
            const auto& p=world_.performance;auto t=p.ticks(),w=p.work_times();std::ostringstream details;details<<std::fixed<<std::setprecision(2)<<"Sim max "<<t.maximum<<" ms | loop work avg/p95/max "<<w.mean<<"/"<<w.p95<<"/"<<w.maximum<<" ms | dropped slots "<<p.dropped_tick_slots;reply(details.str());
            reply("Players "+std::to_string(world_.all_players().size())+" | entities "+std::to_string(world_.simulation().mobs.size())+" | chunks cached "+std::to_string(world_.cached_chunks())+" | edits "+std::to_string(world_.edit_count())+" | water queue "+std::to_string(world_.simulation().pending_water()));
            std::ostringstream io;io<<std::fixed<<std::setprecision(1)<<"Network RX/TX "<<p.receive_rate/1024<<"/"<<p.send_rate/1024<<" KiB/s | queued "<<p.queued_bytes<<" B | connections "<<p.connections<<" | private commit "<<static_cast<double>(p.private_bytes)/1048576<<" MiB";reply(io.str());
            std::ostringstream costs;costs<<std::fixed<<std::setprecision(2)<<"Chunk generation last/max "<<p.last_generation_ms<<"/"<<p.max_generation_ms<<" ms | chunk packet "<<p.last_chunk_ms<<" ms | save "<<p.last_save_ms<<" ms";reply(costs.str());
            reply(std::string("CUDA ")+(gpu::compiled()?"compiled":"unavailable")+" | simulation: CPU | density generator: CPU. GPU utilization is not sampled.");
        } else if(cmd=="monitor") {
            arity(2,sender?2:3);if(a[1]!="on"&&a[1]!="off")usage();
            if(sender){sender->monitor_=a[1]=="on";if(!sender->monitor_)sender->send(Packet(0x77).text("").u8(1));}
            else {if(a.size()==3)monitor_seconds_=integer(a[2],1,60);console_monitor_=a[1]=="on";last_console_=std::chrono::steady_clock::now()-std::chrono::seconds(monitor_seconds_);}
            reply("Performance monitor "+a[1]+(sender?" (action bar every second)":" (console)"));
        } else if(cmd=="ops") {arity(1,1);std::string names="Operators:";for(auto [id,name]:operators_)names+=' '+name;reply(names);}
        else if(cmd=="op"||cmd=="deop") {
            arity(2,2);std::string id,name;for(auto* p:world_.all_players())if(lower(p->profile.name)==lower(a[1])){id=uuid(p->profile);name=p->profile.name;break;}
            if(id.empty()){const auto& source=cmd=="deop"?operators_:known_;for(auto [key,value]:source)if(lower(value)==lower(a[1])){if(!id.empty())throw std::runtime_error("Ambiguous previous name; target must join first");id=key;name=value;}}
            if(id.empty())throw std::runtime_error("Unknown player; let them join once before granting OP");
            auto previous=operators_;if(cmd=="op")operators_[id]=name;else operators_.erase(id);
            try{persist();}catch(...){operators_=std::move(previous);throw;}
            for(auto* p:world_.all_players())if(uuid(p->profile)==id){p->command_tree();p->send(Packet(0x22).u32(static_cast<unsigned>(p->entity_id)).u8(cmd=="op"?28:24));p->tell(cmd=="op"?"You are now an operator.":"Your operator permissions were removed.");}
            reply(name+(cmd=="op"?" is now an operator.":" is no longer an operator."));
        } else if(cmd=="gamemode") {arity(2,3);int mode=a[1]=="survival"||a[1]=="0"?0:a[1]=="creative"||a[1]=="1"?1:-1;if(mode<0)usage();auto* p=player(a.size()==3?a[2]:"@s",sender);p->set_mode(mode);reply("Gamemode updated for "+p->profile.name);}
        else if(cmd=="dimension") {arity(2,3);const int id=a[1]=="overworld"?0:a[1]=="nether"?1:a[1]=="end"?2:-1;if(id<0)usage();auto* p=player(a.size()==3?a[2]:"@s",sender);if(!p->alive())throw std::runtime_error("Respawn first");p->change_dimension(id);reply("Dimension updated for "+p->profile.name);}
        else if(cmd=="time") {arity(3,3);if(a[1]!="set")usage();world_.simulation().day_time=a[2]=="day"?1000:a[2]=="night"?13000:integer(a[2],0,23999);world_.mark_dirty();for(auto* p:world_.all_players())p->send(Packet(0x6f).u64(world_.simulation().tick).u64(world_.simulation().day_time).u8(1));reply("Time set to "+std::to_string(world_.simulation().day_time));}
        else if(cmd=="summon"||cmd=="tp") {
            GameSession* target=sender;std::size_t first=1;double x=sender?sender->x:0,y=sender?sender->y:64,z=sender?sender->z:0;
            if(cmd=="summon") {if(a.size()!=2&&a.size()!=5)usage();if(!mobdata::find(a[1]))throw std::runtime_error("Unknown mob");if(a.size()==2){if(!sender)usage();x+=2;z+=2;}first=2;}
            else {
                if(a.size()==3||a.size()==5){target=player(a[1],sender);first=2;x=target->x;y=target->y;z=target->z;}
                else if(a.size()!=2&&a.size()!=4)usage();
                if(!target)throw std::runtime_error("Console must specify the player to teleport");if(!target->alive())throw std::runtime_error("Target must respawn first");
                if(a.size()-first==1){auto* destination=player(a[first],sender);target->change_dimension(destination->world().dimension_id());x=destination->x;y=destination->y;z=destination->z;}
            }
            if(a.size()-first==3){x=coordinate(a[first],x);y=coordinate(a[first+1],y);z=coordinate(a[first+2],z);}
            if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||std::abs(x)>1023||std::abs(z)>1023||y< -63||y>319)throw std::runtime_error("Coordinates outside world bounds");
            if(cmd=="summon"){if(!(sender?sender->world():world_).simulation().spawn(mobdata::find(a[1])->id,x,y,z))throw std::runtime_error("Entity limit reached");reply("Summoned "+a[1]);}
            else {target->x=x;target->y=y;target->z=z;target->falling_=false;target->fall_start_=y;target->mining_active_=false;target->eat_at_=0;++target->teleport_;target->awaiting_teleport_=true;target->send(Packet(0x46).var(target->teleport_).f64(x).f64(y).f64(z).f64(0).f64(0).f64(0).f32(target->yaw).f32(target->pitch).u32(0));target->update_view();target->save_player();reply("Teleported "+target->profile.name);}
        } else if(cmd=="give") {arity(3,4);auto* p=player(a[1],sender);auto name=a[2];if(name.starts_with("minecraft:"))name.erase(0,10);int item=simdata::item_id(name);if(!item)throw std::runtime_error("Unknown item");if(!p->alive())throw std::runtime_error("Target must respawn first");int count=a.size()==4?integer(a[3],1,64):1;int left=p->add_item({item,count,0});reply("Gave "+std::to_string(count-left)+" "+name+" to "+p->profile.name+(left?" (inventory full)":""));}
        else if(cmd=="place") {
            arity(6,7);if(a[1]!="template")usage();
            double x=coordinate(a[3],sender?sender->x:0),y=coordinate(a[4],sender?sender->y:64),z=coordinate(a[5],sender?sender->z:0);
            if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||x< -1024||x>=1024||z< -1024||z>=1024||y< -63||y>319)throw std::runtime_error("Coordinates outside world bounds");
            reply((sender?sender->world():world_).place_template(a[2],{static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y)),static_cast<int>(std::floor(z))},a.size()==7?integer(a[6],0,255):0));
        }
        else if(cmd=="heal"||cmd=="feed"||cmd=="clear"||cmd=="kill") {
            arity(1,2);auto* p=player(a.size()==2?a[1]:"@s",sender);if(!p->alive())throw std::runtime_error("Target must respawn first");
            if(cmd=="kill")p->damage(1000,"an operator command",true);
            else if(cmd=="clear"){p->inventory_={};p->crafting_={};p->cursor_={};p->mining_active_=false;p->eat_at_=0;p->inventory_sync();}
            else {if(cmd=="heal"){p->health_=20;p->air_=300;}p->food_=20;p->saturation_=5;p->exhaustion_=0;p->health_sync();p->save_player();}
            reply(cmd+" applied to "+p->profile.name);
        } else if(cmd=="kick") {arity(2,64);auto* p=player(a[1],sender);p->kick(a.size()>2?joined_words(a,2):"Kicked by an operator");reply("Kicked "+p->profile.name);}
        else if(cmd=="say") {arity(2,256);std::string text="[Server] "+joined_words(a,1);for(auto* p:world_.all_players())p->tell(text);reply(text);}
        else if(cmd=="pause") {arity(2,2);if(!world_.local_session)throw std::runtime_error("Pause is only available in private local sessions");if(a[1]!="on"&&a[1]!="off")usage();world_.paused=a[1]=="on";reply(world_.paused?"Local simulation paused.":"Local simulation resumed.");}
        else if(cmd=="save-all"||cmd=="stop") {arity(1,1);for(auto* p:world_.all_players())p->save_player();world_.mark_dirty();world_.save();persist();reply("World and players saved.");if(cmd=="stop"){stopping=true;reply("Stopping server...");}}
        if(definition->op)std::cout<<"[ADMIN] "<<(sender?sender->profile.name:"Console")<<": "<<command<<std::endl;
    } catch(const std::exception& e){reply("Error: "+std::string(e.what()));}
}
}
