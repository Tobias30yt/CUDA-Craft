#include "cudacraft/game.hpp"
#include "cudacraft/generated_game_data.hpp"
#include "cudacraft/terrain_ids.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/admin.hpp"
#include "cudacraft/mob_data.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <sstream>

namespace cudacraft {
namespace {
void slot(Packet& p,ItemStack s) { if(s.item&&s.count) {p.var(s.count).var(s.item).var(s.damage?1:0).var(0);if(s.damage)p.var(3).var(s.damage);} else p.var(0); }
std::string player_key(const auth::Profile& profile) {std::string s;for(auto b:profile.uuid){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
bool supported(int item) { return simdata::item(item)!=nullptr; }
std::uint8_t angle(float degrees) { return static_cast<std::uint8_t>(static_cast<int>(std::fmod(degrees,360.0f)*256.0f/360.0f)&255); }
int chunk_coord(double value) { return static_cast<int>(std::floor(value/16)); }
bool replaceable(int state) { return state==0 || simdata::water(state) || state==terrain_ids::short_grass || state==terrain_ids::dandelion || state==terrain_ids::poppy; }
}
GameSession::GameSession(World& world,auth::Profile identity,Send sender)
    :entity_id(world.root().next_entity++),profile(std::move(identity)),world_(&world),send_(std::move(sender)) {}
GameSession::~GameSession() {
    if(!joined_) return;
    send_=[](std::span<const std::uint8_t>){};
    try { close_crafting();save_player(); }
    catch(const std::exception& e) { std::cerr<<"Player save failed: "<<e.what()<<std::endl; }
    std::erase(world_->players,this);
    for(auto* peer:world_->players) {
        try { peer->send(Packet(0x43).var(1).raw(profile.uuid)); peer->send(Packet(0x4b).var(1).var(entity_id)); }
        catch(...) { peer->expired_=true; }
    }
    std::cout<<"Player left: "<<profile.name<<std::endl;
}
void GameSession::player_info(const GameSession& other) {
    Packet p(0x44); p.u8(0x1d).var(1).raw(other.profile.uuid).str(other.profile.name).var(static_cast<int>(other.profile.properties.size()));
    for(const auto& property:other.profile.properties) {
        p.str(property.name).str(property.value).u8(property.signature?1:0);
        if(property.signature) p.str(*property.signature);
    }
    p.var(other.mode_).var(1).var(0);
    send(p);
}
void GameSession::spawn(const GameSession& other) {
    send(Packet(0x01).var(other.entity_id).raw(other.profile.uuid).var(155).f64(other.x).f64(other.y).f64(other.z)
        .u8(0).u8(angle(other.pitch)).u8(angle(other.yaw)).u8(angle(other.yaw)).var(0));
}
void GameSession::start() {
    for(auto* peer:world_->all_players()) if(peer->profile.uuid==profile.uuid) throw protocol::Error("Player already connected");
    const auto home=world_->spawn(); x=home.x+0.5; y=home.y; z=home.z+0.5;
    mode_=world_->options().gamemode;
    const auto saved=world_->simulation().saved_players.find(player_key(profile));
    if(saved==world_->simulation().saved_players.end()&&world_->simulation().saved_players.size()>=1024)throw protocol::Error("Player save capacity reached");
    if(saved!=world_->simulation().saved_players.end()) {
        const auto& p=saved->second;mode_=p.mode;inventory_=p.inventory;health_=p.health;food_=p.food;saturation_=p.saturation;exhaustion_=p.exhaustion;
        crafting_=p.crafting;cursor_=p.cursor;
        world_=&world_->root().dimension(p.dimension);
        if(health_>0) {x=p.x;y=p.y;z=p.z;}
    } else if(mode_==1) for(int i=0;i<9;++i)inventory_[36+i]={game_data::building_blocks[i].item,64,0};
    fall_start_=y;
    try{world_->admin().joined(profile);}catch(const std::exception&){throw protocol::Error("Cannot persist player identity");}
    joined_=true; world_->players.push_back(this);
    Packet join(0x30);
    join.u32(static_cast<std::uint32_t>(entity_id)).u8(0).var(3).str("minecraft:overworld").str("minecraft:the_nether").str("minecraft:the_end").var(20).var(3).var(3)
        .u8(0).u8(1).u8(0).var(world_->dimension_type()).str(world_->dimension_name()).u64(world_->seed_hash()).u8(static_cast<std::uint8_t>(mode_)).u8(255).u8(0).u8(world_->options().generator=="flat"?1:0).u8(0).var(0).var(63).u8(0);
    send(join);
    send(Packet(0x3e).u8(mode_==1?0x0d:0).f32(0.05f).f32(0.1f));
    send(Packet(0x0a).u8(0).u8(1));
    health_sync();
    send(Packet(0x6f).u64(world_->simulation().tick).u64(world_->simulation().day_time).u8(1));
    send(Packet(0x2a).f64(0).f64(0).f64(2048).f64(2048).var(0).var(29999984).var(5).var(15));
    send(Packet(0x5f).str(world_->dimension_name()).pos(world_->spawn()).f32(0).f32(0));
    send(Packet(0x26).u8(13).f32(0));
    for(auto* peer:world_->players) {
        player_info(*peer);
        if(peer!=this) {
            spawn(*peer);
            try { peer->player_info(*this); peer->spawn(*this); }
            catch(...) { peer->expired_=true; }
        }
    }
    close_crafting();inventory_sync();
    send(Packet(0x67).var(0));
    command_tree();send(Packet(0x22).u32(static_cast<unsigned>(entity_id)).u8(world_->admin().is_operator(profile)?28:24));
    send(Packet(0x46).var(teleport_).f64(x).f64(y).f64(z).f64(0).f64(0).f64(0).f32(yaw).f32(pitch).u32(0));
    update_view();
    tell("CUDA Craft: "+std::string(survival()?"survival":"creative")+". Prototype dimensions, 89 mob types, water and basic redstone. /help");
    if(world_->options().generator=="overworld") tell("Experimental overworld, seed "+std::to_string(world_->options().seed)+". Terrain is not identical to vanilla seeds.");
    if(world_->options().generator=="climate") tell("1.21.11 climate biomes, seed "+std::to_string(world_->options().seed)+". Terrain and decorations are experimental.");
    if(world_->options().generator=="density") tell("1.21.11 terrain, seed "+std::to_string(world_->options().seed)+". Vegetation and gameplay are still incomplete vanilla implementations.");
    std::cout<<"Player joined "<<(survival()?"survival":"creative")<<" world: "<<profile.name<<std::endl;
    if(!alive())send(Packet(0x42).var(entity_id).text("You died."));
}
void GameSession::tell(std::string_view message) { send(Packet(0x77).text(message).u8(0)); }
void GameSession::kick(std::string_view reason){if(!kicked_){save_player();send(Packet(0x20).text(reason));kicked_=true;}}
void GameSession::command_tree() {
    const auto names=world_->admin().commands(this);Packet p(0x10);p.var(static_cast<int>(names.size()*2+1)).u8(0).var(static_cast<int>(names.size()));
    for(int i=0;i<static_cast<int>(names.size());++i)p.var(i*2+1);
    for(int i=0;i<static_cast<int>(names.size());++i){p.u8(5).var(1).var(i*2+2).str(names[i]);p.u8(22).var(0).str("arguments").var(5).var(2).str("minecraft:ask_server");}
    p.var(0);send(p);
}
void GameSession::update_view() {
    const int cx=chunk_coord(x),cz=chunk_coord(z);
    if(cx==center_x_ && cz==center_z_) return;
    center_x_=cx; center_z_=cz;
    send(Packet(0x5c).var(cx).var(cz));
    for(auto it=loaded_.begin();it!=loaded_.end();) {
        if(std::abs(it->first-cx)>3 || std::abs(it->second-cz)>3) {
            send(Packet(0x25).u32(static_cast<std::uint32_t>(it->second)).u32(static_cast<std::uint32_t>(it->first)));
            it=loaded_.erase(it);
        } else ++it;
    }
    pending_.clear();
    for(int distance=0;distance<=3;++distance) for(int dz=-distance;dz<=distance;++dz) for(int dx=-distance;dx<=distance;++dx) {
        if(std::max(std::abs(dx),std::abs(dz))!=distance) continue;
        const auto key=std::pair(cx+dx,cz+dz);
        if(!loaded_.contains(key)) pending_.push_back(key);
    }
}
void GameSession::teleport_home() {
    const auto home=world_->spawn(); x=home.x+0.5; y=home.y; z=home.z+0.5; ++teleport_; awaiting_teleport_=true;
    falling_=false;fall_start_=y;mining_active_=false;eat_at_=0;
    send(Packet(0x46).var(teleport_).f64(x).f64(y).f64(z).f64(0).f64(0).f64(0).f32(0).f32(0).u32(0));
    update_view();save_player();
}
void GameSession::set_slot(int index,int item,int count,int damage_value) {
    if(index<0 || index>=46) return;
    const auto* def=simdata::item(item);
    inventory_[static_cast<std::size_t>(index)]=item&&def&&count>0?ItemStack{item,std::min(count,def->stack),damage_value}:ItemStack{};
    if(crafting_open_||!container_positions_.empty()){inventory_sync();return;}
    Packet p(0x14); p.var(0).var(++inventory_revision_).u16(static_cast<std::uint16_t>(index)); slot(p,inventory_[index]); send(p);
    save_player();
}
bool GameSession::reachable(BlockPos p) const {
    if(!World::valid(p) || !loaded_.contains({chunk_coord(p.x),chunk_coord(p.z)})) return false;
    const double dx=p.x+0.5-x, dy=p.y+0.5-y-1.62, dz=p.z+0.5-z;
    return dx*dx+dy*dy+dz*dz<=49;
}
void GameSession::block_changed(BlockPos p,int state) {
    if(!loaded_.contains({chunk_coord(p.x),chunk_coord(p.z)})) return;
    try { send(Packet(0x08).pos(p).var(state)); } catch(...) { expired_=true; }
}
void GameSession::poll() {
    if(!container_positions_.empty()&&!container_valid()){close_crafting();inventory_sync();}
    const auto now=std::chrono::steady_clock::now();
    if(keepalive_ && now-last_keepalive_>std::chrono::seconds(20)) { expired_=true; return; }
    if(!keepalive_ && now-last_keepalive_>std::chrono::seconds(5)) {
        keepalive_=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count());
        last_keepalive_=now; send(Packet(0x2b).u64(keepalive_));
    }
    if((!pending_.empty()||!refresh_.empty()) && now-last_chunks_>=std::chrono::milliseconds(50)) {
        last_chunks_=now;
        send(Packet(0x0c));
        int count=0;
        while((!pending_.empty()||!refresh_.empty()) && count<2) {
            std::pair<int,int> key;
            if(!refresh_.empty()){key=*refresh_.begin();refresh_.erase(refresh_.begin());if(!loaded_.contains(key))continue;}
            else {key=pending_.front();pending_.pop_front();}
            auto [cx,cz]=key;
            world_->activate_chunk(cx,cz);
            const auto chunk_start=Performance::Clock::now();send_(world_->chunk(cx,cz));
            world_->performance.last_chunk_ms=Performance::elapsed(chunk_start);++world_->performance.chunks_sent;
            loaded_.insert({cx,cz}); ++count;
        }
        send(Packet(0x0b).var(count));
    }
    if(!light_refresh_.empty()&&now-last_light_>=std::chrono::milliseconds(50)) {
        last_light_=now;
        for(int count=0;count<2&&!light_refresh_.empty();++count) {
            auto [cx,cz]=*light_refresh_.begin();
            light_refresh_.erase(light_refresh_.begin());
            if(sees(cx,cz))send_(world_->chunk(cx,cz,true));
        }
    }
    sync_mobs();
}
void GameSession::handle(std::span<const std::uint8_t> payload) {
    if(kicked_)return;
    protocol::Reader r(payload);
    const int id=r.varint();
    if(id==0x00) {
        const int received=r.varint(); r.require_end();
        if(received!=teleport_) throw protocol::Error("Unexpected teleport acknowledgement");
        awaiting_teleport_=false;
    } else if(id==0x1b) {
        const auto received=read_u64(r); r.require_end();
        if(!keepalive_ || received!=keepalive_) throw protocol::Error("Invalid keepalive");
        keepalive_=0; last_keepalive_=std::chrono::steady_clock::now();
    } else if(id>=0x1d && id<=0x20) {
        double nx=x,ny=y,nz=z; float new_yaw=yaw,new_pitch=pitch;
        if(id==0x1d || id==0x1e) { nx=read_f64(r); ny=read_f64(r); nz=read_f64(r); }
        if(id==0x1e || id==0x1f) { new_yaw=read_f32(r); new_pitch=read_f32(r); }
        const auto flags=r.bytes(1)[0]; r.require_end();
        if(!std::isfinite(nx)||!std::isfinite(ny)||!std::isfinite(nz)||!std::isfinite(new_yaw)||!std::isfinite(new_pitch)) throw protocol::Error("Nonfinite movement");
        if(awaiting_teleport_) return;
        if(!alive())return;
        if(std::abs(nx)>1023 || std::abs(nz)>1023 || ny< -65 || ny>400 || std::abs(nx-x)>64 || std::abs(nz-z)>64 || std::abs(ny-y)>64) { teleport_home(); return; }
        if(survival()) {
            exhaustion_+=static_cast<float>(std::hypot(nx-x,nz-z)*.01);
            const bool wet=simdata::water(world_->block({static_cast<int>(std::floor(nx)),static_cast<int>(std::floor(ny)),static_cast<int>(std::floor(nz))}));
            const bool landed=world_->solid({static_cast<int>(std::floor(nx)),static_cast<int>(std::floor(ny-.01)),static_cast<int>(std::floor(nz))});
            if(wet){falling_=false;fall_start_=ny;}
            else if(landed) {if(falling_&&fall_start_-ny>3)damage(static_cast<float>(std::ceil(fall_start_-ny-3)),"a fall");falling_=false;fall_start_=ny;}
            else {if(!falling_)fall_start_=y;falling_=true;fall_start_=std::max(fall_start_,ny);}
        }
        x=nx;y=ny;z=nz; yaw=std::fmod(new_yaw,360.0f); pitch=std::clamp(new_pitch,-90.0f,90.0f); update_view();save_player();
        for(auto* peer:world_->players) if(peer!=this) {
            try { peer->send(Packet(0x23).var(entity_id).f64(x).f64(y).f64(z).f64(0).f64(0).f64(0).f32(yaw).f32(pitch).u8(flags&1)); peer->send(Packet(0x51).var(entity_id).u8(angle(yaw))); }
            catch(...) { peer->expired_=true; }
        }
    } else if(id==0x34) {
        const int index=static_cast<std::int16_t>(r.u16()); r.require_end();
        if(index<0 || index>8) throw protocol::Error("Invalid hotbar slot"); selected_=index;
    } else if(id==0x37) {
        if(survival()) {inventory_sync();return;}
        const int index=static_cast<std::int16_t>(r.u16());
        int count=r.varint(),item=0;
        bool plain=true;int damage_value=0;
        if(count<0 || count>64) throw protocol::Error("Invalid creative item count");
        if(count) {
            item=r.varint(); const int added=r.varint(),removed=r.varint();
            if(added<0 || added>64 || removed<0 || removed>64) throw protocol::Error("Invalid item components");
            plain=removed==0;
            for(int i=0;i<added;++i) { const int type=r.varint(); if(type==3)damage_value=r.varint();else {inventory_sync();tell("Custom item components are not supported yet.");return;} }
            for(int i=0;i<removed;++i) (void)r.varint();
        }
        r.require_end();
        if(index==-1) {if(item&&plain&&supported(item))world_->simulation().spawn(71,x,y+1,z,{item,count,0});return;}
        if(index<1 || index>=46) throw protocol::Error("Invalid creative slot");
        const auto* def=simdata::item(item);
        if(!plain || (item && (!supported(item)||!def||damage_value<0||damage_value>def->durability))) { item=0; tell("This item is not supported yet."); }
        set_slot(index,item,count,damage_value);
    } else if(id==0x28) {
        const int action=r.varint(); const auto p=unpack_position(read_u64(r)); (void)r.bytes(1); const int sequence=r.varint(); r.require_end();
        if(sequence<0) throw protocol::Error("Invalid block sequence");
        if(action==1)mining_active_=false;
        if((action==0 || action==2) && !awaiting_teleport_ && alive()&&reachable(p)) {
            if(!survival())(void)world_->simulation().break_block(p);
            else if(action==0) {mining_=p;mining_state_=world_->block(p);mining_start_=world_->simulation().tick;mining_active_=true;if(simdata::block(mining_state_).hardness==0)finish_mining(p);}
            else finish_mining(p);
        }
        if((action==3||action==4)&&alive()) {
            auto& held=inventory_[36+selected_];int count=action==3?held.count:1;
            if(held.item&&world_->simulation().spawn(71,x,y+1,z,{held.item,count,held.damage}))set_slot(36+selected_,held.item,held.count-count,held.damage);
        }
        if(action==5){eat_at_=0;eating_slot_=-1;send(Packet(0x61).var(entity_id).u8(8).var(0).u8(0).u8(255));}
        if(action==6&&alive()) {
            std::swap(inventory_[36+selected_],inventory_[45]);
            eat_at_=0;eating_slot_=-1;mining_active_=false;inventory_sync();
        }
        block_changed(p,world_->block(p)); send(Packet(0x04).var(sequence));
    } else if(id==0x3f) {
        const int hand=r.varint(); const auto clicked=unpack_position(read_u64(r)); const int face=r.varint();
        float cursor_y=0;
        for(int i=0;i<3;++i) {const float cursor=read_f32(r);if(!std::isfinite(cursor)||cursor<0||cursor>1) throw protocol::Error("Invalid cursor");if(i==1)cursor_y=cursor;}
        (void)r.bytes(2); const int sequence=r.varint(); r.require_end();
        if(face<0 || face>5 || hand<0 || hand>1 || sequence<0) throw protocol::Error("Invalid placement");
        const int held_slot=hand?45:36+selected_;
        if(!awaiting_teleport_&&alive()&&reachable(clicked)) {
            const int clicked_state=world_->block(clicked);
            if(std::string_view(simdata::block(clicked_state).name)=="jukebox"&&
               world_->simulation().use_jukebox(clicked,inventory_[held_slot])) {
                if(survival()&&simdata::property_value(clicked_state,"has_record")=="false")
                    set_slot(held_slot,inventory_[held_slot].item,inventory_[held_slot].count-1);
                send(Packet(0x04).var(sequence));return;
            }
            if(open_container(clicked)){send(Packet(0x04).var(sequence));return;}
            if(world_->simulation().interact(clicked)) {send(Packet(0x04).var(sequence));return;}
            if(world_->block(clicked)==simdata::crafting_table_state) {
                close_crafting();crafting_open_=true;send(Packet(0x39).var(1).var(12).text("Crafting"));inventory_sync();send(Packet(0x04).var(sequence));return;
            }
            if(use_bucket(clicked,face,held_slot)) {send(Packet(0x04).var(sequence));return;}
            if(use_portal_item(clicked,face,held_slot)) {send(Packet(0x04).var(sequence));return;}
            int item=inventory_[held_slot].item;
            if(const auto* def=simdata::item(item);def&&std::string_view(def->name).ends_with("_spawn_egg")) {
                std::string_view name=def->name;name.remove_suffix(10);
                if(const auto* kind=mobdata::find(name);kind&&world_->simulation().spawn(kind->id,clicked.x+.5,clicked.y+1,clicked.z+.5)&&survival())set_slot(held_slot,item,inventory_[held_slot].count-1);
                send(Packet(0x04).var(sequence));return;
            }
        }
        constexpr int directions[6][3]={{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
        BlockPos target{clicked.x+directions[face][0],clicked.y+directions[face][1],clicked.z+directions[face][2]};
        if(world_->block(clicked)!=0 && replaceable(world_->block(clicked))) target=clicked;
        int state=world_->simulation().placement(inventory_[held_slot].item,face,yaw,pitch);
        const bool top=face==0||(face>=2&&cursor_y>.5f);
        state=simdata::with_property(state,"half",top?"top":"bottom");
        if(std::string_view(simdata::block(state).name).ends_with("_slab"))state=simdata::with_property(state,"type",top?"top":"bottom");
        state=simdata::with_property(state,"waterlogged",simdata::water(world_->block(target))?"true":"false");
        bool merging=false;
        if(std::string_view(simdata::block(state).name).ends_with("_slab")) {
            auto same_slab=[&](BlockPos p){const int old=world_->block(p);return simdata::block(old).first==simdata::block(state).first&&simdata::property_value(old,"type")!="double";};
            const auto half=simdata::property_value(world_->block(clicked),"type");
            if(same_slab(clicked)&&((half=="bottom"&&face==1)||(half=="top"&&face==0)||(face>=2&&((half=="bottom"&&cursor_y>.5f)||(half=="top"&&cursor_y<=.5f))))) {target=clicked;merging=true;}
            else if(same_slab(target))merging=true;
            if(merging){state=simdata::with_property(state,"type","double");state=simdata::with_property(state,"waterlogged","false");}
        }
        bool intersects=false;
        for(const auto* player:world_->players) if(target.x+1>player->x-0.3 && target.x<player->x+0.3 && target.z+1>player->z-0.3 && target.z<player->z+0.3 && target.y+1>player->y && target.y<player->y+1.8) intersects=true;
        bool support=true;
        if(simdata::wire(state)||simdata::repeater(state)||std::string_view(simdata::block(state).name)=="comparator"||
           state==simdata::redstone_torch_state||state==3169)support=world_->solid({target.x,target.y-1,target.z});
        if(!awaiting_teleport_ && alive()&&state && reachable(clicked) && World::valid(target) && world_->block(clicked)!=0 && (replaceable(world_->block(target))||merging) && (!intersects||!simdata::block(state).solid) && support) {
            if(world_->simulation().place_block(target,state)&&survival())set_slot(held_slot,inventory_[held_slot].item,inventory_[held_slot].count-1,inventory_[held_slot].damage);
        }
        block_changed(clicked,world_->block(clicked)); block_changed(target,world_->block(target)); send(Packet(0x04).var(sequence));
    } else if(id==0x23) {
        const auto p=unpack_position(read_u64(r)); (void)r.bytes(1); r.require_end();
        if(!survival()&&reachable(p)) set_slot(36+selected_,World::state_item(world_->block(p)));
    } else if(id==0x04) {
        const int mode=r.varint();r.require_end();if(mode<0||mode>3)throw protocol::Error("Invalid requested gamemode");
        if(!world_->admin().is_operator(profile))tell("Permission denied: operator required.");
        else if(mode>1)tell("Only survival and creative are implemented; adventure and spectator are not available yet.");
        else world_->admin().execute(mode==0?"gamemode survival":"gamemode creative",this,[this](const std::string& text){tell(text);});
        send(Packet(0x26).u8(3).f32(static_cast<float>(mode_)));
        send(Packet(0x3e).u8(mode_==1?0x0d:0).f32(.05f).f32(.1f));
    } else if(id==0x06||id==0x07) {
        const auto command=r.string(256);
        if(id==0x07) {
            (void)read_u64(r);(void)read_u64(r);const int signatures=r.varint();
            if(signatures<0||signatures>16)throw protocol::Error("Invalid command signature count");
            for(int i=0;i<signatures;++i){(void)r.string(64);(void)r.bytes(256);}
            if(r.varint()<0)throw protocol::Error("Invalid command acknowledgement count");
            (void)r.bytes(4); // Three acknowledgement bytes and one checksum.
        }
        r.require_end();
        world_->admin().execute(command,this,[this](const std::string& text){tell(text);});
    } else if(id==0x0e) {
        const int transaction=r.varint(); const auto text=r.string(256); r.require_end();
        const std::string prefix=text.starts_with('/')?text.substr(1):text;
        std::vector<std::string> matches;
        matches=world_->admin().suggestions(prefix,this);
        Packet response(0x0f); response.var(transaction).var(text.starts_with('/')?1:0).var(static_cast<int>(prefix.size())).var(static_cast<int>(matches.size()));
        for(const auto& match:matches) response.str(match).u8(0);
        send(response);
    } else if(id==0x08) {
        const auto message=r.string(256);
        for(auto* peer:world_->players) {
            try { peer->tell("<"+profile.name+"> "+message); } catch(...) { peer->expired_=true; }
        }
    } else if(id==0x25) { const auto value=read_u64(r); r.require_end(); send(Packet(0x3c).u64(value)); }
    else if(id==0x0b) {const int action=r.varint();r.require_end();if(action==0&&!alive())respawn();}
    else if(id==0x0a) { const float rate=read_f32(r); r.require_end(); if(!std::isfinite(rate)) throw protocol::Error("Invalid chunk batch feedback"); }
    else if(id==0x11)inventory_click(r);
    else if(id==0x12) { const int window=r.varint();r.require_end();if(window==current_window())close_crafting();inventory_sync(); }
    else if(id==0x2d) {
        // Recipe-book UI preferences are valid even without recipe auto-fill.
        const int book=r.varint();const auto flags=r.bytes(2);r.require_end();
        if(book<0||book>3||flags[0]>1||flags[1]>1)throw protocol::Error("Invalid recipe-book settings");
    } else if(id==0x2e) {const int recipe=r.varint();r.require_end();if(recipe<0)throw protocol::Error("Invalid displayed recipe");}
    else if(id==0x19) {
        int target=r.varint(),action=r.varint();if(action==2)for(int i=0;i<3;++i)(void)read_f32(r);
        if(action==0||action==2)(void)r.varint();(void)r.bytes(1);r.require_end();if(action==1)world_->simulation().attack(*this,target);
    } else if(id==0x40) {const int hand=r.varint(),sequence=r.varint();(void)read_f32(r);(void)read_f32(r);r.require_end();if(hand<0||hand>1||sequence<0)throw protocol::Error("Invalid use item");if(alive())use_item(hand);send(Packet(0x04).var(sequence));}
    else if(id==0x27 || id==0x29 || id==0x2a || id==0x2b || id==0x0c || id==0x0d || id==0x15 || id==0x09 || id==0x05 || id==0x3c || id==0x12 || id==0x31 || id==0x2c || id==0x07) {
        // Bounded optional client notifications; no corresponding simulation subsystem yet.
    } else throw protocol::Error("Unsupported play packet " + std::to_string(id));
}
}
