#include "cudacraft/game.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/mob_data.hpp"
#include <algorithm>
#include <cmath>
#include <string>

namespace cudacraft {
namespace {
void item_packet(Packet& p,ItemStack s) {if(!s.item||!s.count){p.var(0);return;}p.var(s.count).var(s.item).var(s.damage?1:0).var(0);if(s.damage)p.var(3).var(s.damage);}
std::string key(const auth::Profile& p){std::string s;for(auto b:p.uuid){s+="0123456789abcdef"[b>>4];s+="0123456789abcdef"[b&15];}return s;}
BlockPos position(double x,double y,double z){return {static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y)),static_cast<int>(std::floor(z))};}
int food_value(int item){using namespace simdata;if(item==bread_item)return 5;if(item==apple_item)return 4;if(item==cooked_beef_item)return 8;if(item==porkchop_item)return 3;return 0;}
float food_saturation(int item){using namespace simdata;if(item==bread_item)return 6;if(item==apple_item)return 2.4f;if(item==cooked_beef_item)return 12.8f;if(item==porkchop_item)return 1.8f;return 0;}
bool same_item(ItemStack a,ItemStack b){return a.item==b.item&&a.damage==b.damage;}
}
void GameSession::save_player() {
    PlayerState p;p.inventory=inventory_;p.x=x;p.y=std::clamp(y,-64.0,399.0);p.z=z;p.health=health_;p.food=food_;p.saturation=saturation_;p.exhaustion=exhaustion_;p.mode=mode_;
    p.crafting=crafting_;p.cursor=cursor_;p.dimension=world_->dimension_id();
    world_->root().simulation().saved_players[key(profile)]=p;world_->root().mark_dirty();
}
void GameSession::health_sync(){send(Packet(0x66).f32(health_).var(food_).f32(saturation_));}
void GameSession::inventory_sync() {
    reset_drag();
    // Offhand is outside chest/workbench menus. Use the 1.21.11 player inventory
    // packet (raw inventory slot 40), so corrections reach it in every window.
    Packet offhand(0x6a);offhand.var(40);item_packet(offhand,inventory_[45]);send(offhand);
    if(!container_positions_.empty()) {
        if(!container_valid()){close_crafting();inventory_sync();return;}
        const int count=Simulation::container_slots(container_type_);
        Packet p(0x12);p.var(container_window_).var(++inventory_revision_).var(static_cast<int>(container_positions_.size())*count+36);
        for(auto position:container_positions_)for(int i=0;i<count;++i)item_packet(p,world_->simulation().container(position)->items[i]);
        for(int i=9;i<=44;++i)item_packet(p,inventory_[i]);item_packet(p,cursor_);send(p);container_properties();save_player();return;
    }
    update_recipe();
    Packet p(0x12);p.var(crafting_open_?1:0).var(++inventory_revision_).var(46);
    if(crafting_open_) {
        item_packet(p,craft_result_);for(auto s:crafting_)item_packet(p,s);
        for(int i=9;i<=44;++i)item_packet(p,inventory_[i]);
    } else for(int i=0;i<46;++i)item_packet(p,i==0?craft_result_:i>=1&&i<=4?crafting_[i-1]:inventory_[i]);
    item_packet(p,cursor_);send(p);save_player();
}
int GameSession::add_item(ItemStack s,bool synchronize) {
    const auto* def=simdata::item(s.item);if(!def||s.count<1)return s.count;
    const int original=s.count;
    for(int pass=0;pass<2;++pass)for(int j=0;j<36&&s.count;++j) {
        int i=j<9?36+j:j;auto& dst=inventory_[i];
        if((pass==0&&dst.item&&same_item(dst,s))||(pass==1&&!dst.item)) {
            int move=std::min(s.count,def->stack-dst.count);if(move<=0)continue;
            dst={s.item,dst.count+move,s.damage};s.count-=move;
        }
    }
    if(s.count!=original&&synchronize)inventory_sync();return s.count;
}
void GameSession::set_mode(int mode) {
    mode_=mode;mining_active_=false;eat_at_=0;falling_=false;fall_start_=y;
    send(Packet(0x26).u8(3).f32(static_cast<float>(mode)));
    send(Packet(0x3e).u8(mode==1?0x0d:0).f32(.05f).f32(.1f));
    for(auto* peer:world_->players) {Packet p(0x44);p.u8(4).var(1).raw(profile.uuid).var(mode);peer->send(p);}
    health_sync();save_player();tell(mode==0?"Survival enabled.":"Creative enabled.");
}
void GameSession::damage(float amount,std::string_view cause,bool force) {
    const auto now=world_->simulation().tick;
    if(!alive()||amount<=0||(!force&&(!survival()||now<hurt_until_)))return;
    hurt_until_=now+10;health_=std::max(0.f,health_-amount);health_sync();
    send(Packet(0x29).var(entity_id).f32(0));
    for(auto* peer:world_->players)if(peer!=this)peer->send(Packet(0x29).var(entity_id).f32(0));
    if(!alive()) {
        close_crafting();mining_active_=false;eat_at_=0;
        for(auto& s:inventory_)if(s.item&&world_->simulation().spawn(71,x,y+.5,z,s))s={};
        inventory_sync();send(Packet(0x42).var(entity_id).text("You died from "+std::string(cause)+"."));
    }
    save_player();
}
void GameSession::respawn() {
    if(world_->dimension_id()){health_=20;food_=20;saturation_=5;exhaustion_=0;air_=300;change_dimension(0);return;}
    health_=20;food_=20;saturation_=5;exhaustion_=0;air_=300;falling_=false;hurt_until_=world_->simulation().tick+40;
    send(Packet(0x50).var(0).str("minecraft:overworld").u64(world_->seed_hash()).u8(static_cast<std::uint8_t>(mode_)).u8(static_cast<std::uint8_t>(mode_)).u8(0).u8(world_->options().generator=="flat").u8(0).var(0).var(63).u8(0));
    loaded_.clear();light_refresh_.clear();seen_mobs_.clear();center_x_=center_z_=100000;
    teleport_home();send(Packet(0x3e).u8(mode_==1?0x0d:0).f32(.05f).f32(.1f));send(Packet(0x26).u8(13).f32(0));
    health_sync();inventory_sync();save_player();
}
void GameSession::wear_tool() {
    if(!survival())return;auto& s=inventory_[36+selected_];const auto* d=simdata::item(s.item);
    if(!d||d->durability==0)return;
    ++s.damage;if(s.damage>=d->durability)s={};
    set_slot(36+selected_,s.item,s.count,s.damage);
}
float GameSession::attack_damage() {
    const auto now=world_->simulation().tick;if(now<attack_after_)return 0;attack_after_=now+12;
    if(!survival())return 100;
    const auto* d=simdata::item(inventory_[36+selected_].item);float result=1;
    if(d&&d->tool==4)result=static_cast<float>(3+d->tier);
    else if(d&&d->tool==2)result=static_cast<float>(6+d->tier);
    wear_tool();exhaustion_+=.1f;return result;
}
void GameSession::finish_mining(BlockPos p) {
    if(!mining_active_||p!=mining_||mining_state_!=world_->block(p)||!reachable(p))return;
    const auto& b=simdata::block(mining_state_);if(b.hardness<0||simdata::water(mining_state_)||simdata::lava(mining_state_))return;
    const auto* tool=simdata::item(inventory_[36+selected_].item);
    const bool proper=tool&&tool->tool==b.tool;
    float speed=proper?static_cast<float>(tool->tier*2):1;
    int required=static_cast<int>(std::ceil(b.hardness*(b.tool==1&&!proper?100.f:30.f)/speed));
    if(world_->simulation().tick-mining_start_<static_cast<unsigned>(std::max(0,required-1)))return;
    int drop=b.drop;
    if(b.tool==1&&!proper)drop=0;
    // Minimum tiers for the most common progression ores.
    const std::string_view name=b.name;
    if(name.find("diamond_ore")!=name.npos||name.find("gold_ore")!=name.npos||name.find("redstone_ore")!=name.npos||name.find("emerald_ore")!=name.npos) {if(!tool||tool->tier<3)drop=0;}
    if(name.find("iron_ore")!=name.npos) {if(!tool||tool->tier<2)drop=0;}
    if(world_->simulation().break_block(p)) {
        if(drop)world_->simulation().spawn(71,p.x+.5,p.y+.5,p.z+.5,{drop,name.ends_with("_slab")&&simdata::property_value(mining_state_,"type")=="double"?2:1,0});
        if(b.hardness>0)wear_tool();exhaustion_+=.005f;
    }
    mining_active_=false;
}
bool GameSession::use_bucket(BlockPos clicked,int face,int index) {
    using namespace simdata;
    auto held=inventory_[index];if(held.item!=bucket_item&&held.item!=water_bucket_item)return false;
    constexpr BlockPos dirs[]={{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
    const int clicked_state=world_->block(clicked);
    if(held.item==bucket_item&&(clicked_state==86||property_value(clicked_state,"waterlogged")=="true")) {
        if(world_->set(clicked,clicked_state==86?0:with_property(clicked_state,"waterlogged","false"))&&survival()) {
            set_slot(index,held.item,held.count-1);
            if(!inventory_[index].item)set_slot(index,water_bucket_item,1);else if(add_item({water_bucket_item,1,0}))world_->simulation().spawn(71,x,y,z,{water_bucket_item,1,0});
        }
        return true;
    }
    if(held.item==water_bucket_item) {
        if(world_->dimension_id()==1){if(survival())set_slot(index,bucket_item,1);return true;}
        if(property_value(clicked_state,"waterlogged")=="false") {
            if(world_->set(clicked,with_property(clicked_state,"waterlogged","true"))&&survival())set_slot(index,bucket_item,1);
            return true;
        }
        BlockPos target{clicked.x+dirs[face].x,clicked.y+dirs[face].y,clicked.z+dirs[face].z};
        if(simdata::replaceable(world_->block(clicked))&&!water(world_->block(clicked)))target=clicked;
        if(World::valid(target)&&simdata::replaceable(world_->block(target))&&world_->set(target,86)&&survival())set_slot(index,bucket_item,1);
        return true;
    }
    return true;
}
void GameSession::use_item(int hand) {
    int index=hand?45:36+selected_;auto held=inventory_[index];
    if(held.item==simdata::bucket_item||held.item==simdata::water_bucket_item) {
        const double a=yaw*3.141592653589793/180,b=pitch*3.141592653589793/180;
        const double dx=-std::sin(a)*std::cos(b),dy=-std::sin(b),dz=std::cos(a)*std::cos(b);
        BlockPos previous=position(x,y+1.62,z);
        for(double distance=.1;distance<=5;distance+=.1) {
            auto p=position(x+dx*distance,y+1.62+dy*distance,z+dz*distance);int state=world_->block(p);
            bool hit=held.item==simdata::bucket_item?state==86:!simdata::replaceable(state);
            if(hit&&reachable(p)) {
                int face=previous.y>p.y?1:previous.y<p.y?0:previous.z<p.z?2:previous.z>p.z?3:previous.x<p.x?4:5;
                use_bucket(p,face,index);return;
            }
            previous=p;
        }
    } else if(food_value(held.item)>0&&survival()&&food_<20&&!eat_at_) {
        eating_slot_=index;eating_item_=held;eat_at_=world_->simulation().tick+32;
        send(Packet(0x61).var(entity_id).u8(8).var(0).u8(hand?3:1).u8(255));
    }
}
void GameSession::simulation_tick() {
    const auto now=world_->simulation().tick;
    if(alive()&&!awaiting_teleport_) {
        const auto name=std::string_view(simdata::block(world_->block(position(x,y,z))).name);
        if((name=="nether_portal"||name=="end_portal")&&now>=portal_cooldown_) {
            if(++portal_ticks_>=(name=="end_portal"||!survival()?1:80)) {change_dimension(name=="end_portal"?(world_->dimension_id()==2?0:2):(world_->dimension_id()==1?0:1),name=="nether_portal");return;}
        } else portal_ticks_=0;
    }
    if(now%20==0)send(Packet(0x6f).u64(now).u64(world_->simulation().day_time).u8(1));
    if(!survival()||!alive())return;
    if(eat_at_&&now>=eat_at_) {
        auto s=inventory_[eating_slot_];
        if(same_item(s,eating_item_)&&s.count>0&&(eating_slot_==45||eating_slot_==36+selected_)) {
            int value=food_value(s.item);food_=std::min(20,food_+value);saturation_=std::min(static_cast<float>(food_),saturation_+food_saturation(s.item));
            set_slot(eating_slot_,s.item,s.count-1,s.damage);send(Packet(0x22).u32(static_cast<unsigned>(entity_id)).u8(9));health_sync();
        }
        eat_at_=0;eating_slot_=-1;send(Packet(0x61).var(entity_id).u8(8).var(0).u8(0).u8(255));
    }
    while(exhaustion_>=4) {exhaustion_-=4;if(saturation_>0)saturation_=std::max(0.f,saturation_-1);else food_=std::max(0,food_-1);health_sync();}
    const int head=world_->block(position(x,y+1.62,z)),feet=world_->block(position(x,y,z));
    if(simdata::water(head)) {--air_;if(air_< -20){air_=0;damage(2,"drowning");}}else air_=std::min(300,air_+4);
    if(now%20==0)send(Packet(0x61).var(entity_id).u8(1).var(1).var(std::max(0,air_)).u8(255));
    if(simdata::lava(feet))damage(4,"lava");
    if(y< -63)damage(4,"the void");
    if(now%80==0) {
        if(food_>=18&&health_<20){health_=std::min(20.f,health_+1);exhaustion_+=6;health_sync();}
        if(food_==0&&health_>1)damage(1,"starvation");
    }
    if(now%20==0)save_player();
}
void GameSession::sync_mobs() {
    const auto now=world_->simulation().tick;
    if(last_mob_sync_==now)return;
    last_mob_sync_=now;
    for(auto i=seen_mobs_.begin();i!=seen_mobs_.end();) {
        auto found=world_->simulation().mobs.find(*i);
        if(found==world_->simulation().mobs.end()||!sees(static_cast<int>(std::floor(found->second.x/16)),static_cast<int>(std::floor(found->second.z/16)))) {send(Packet(0x4b).var(1).var(*i));i=seen_mobs_.erase(i);}else ++i;
    }
    for(const auto& [id,m]:world_->simulation().mobs) {
        if(!sees(static_cast<int>(std::floor(m.x/16)),static_cast<int>(std::floor(m.z/16))))continue;
        if(seen_mobs_.insert(id).second) {
            auto uuid=Packet().u64(world_->seed_hash()).u64(static_cast<std::uint64_t>(id));
            send(Packet(0x01).var(id).raw(uuid.data).var(m.type).f64(m.x).f64(m.y).f64(m.z).u8(0).u8(0).u8(0).u8(0).var(0));
            Packet metadata(0x61);metadata.var(id);
            if(m.type==71) {metadata.u8(8).var(7);item_packet(metadata,m.drop);}
            else if(const auto* kind=mobdata::find(m.type))metadata.u8(static_cast<std::uint8_t>(kind->health_index)).var(3).f32(m.health);
            metadata.u8(255);send(metadata);
        } else if(now%4==0||m.vx!=0||m.vy!=0||m.vz!=0)send(Packet(0x23).var(id).f64(m.x).f64(m.y).f64(m.z).f64(m.vx).f64(m.vy).f64(m.vz).f32(m.yaw).f32(0).u8(m.vy==0));
    }
}
void GameSession::mob_hurt(int id){if(seen_mobs_.contains(id))send(Packet(0x29).var(id).f32(0));}
void GameSession::collect(int mob,int player,int count){if(seen_mobs_.contains(mob))send(Packet(0x7a).var(mob).var(player).var(count));}

void GameSession::close_crafting() {
    reset_drag();
    const int window=current_window();crafting_open_=false;container_positions_.clear();container_type_=0;
    for(auto& s:crafting_)if(s.item) {auto temp=s;s={};int left=add_item(temp,false);if(left&&!world_->simulation().spawn(71,x,y+.5,z,{temp.item,left,temp.damage}))s={temp.item,left,temp.damage};}
    if(cursor_.item) {auto temp=cursor_;cursor_={};int left=add_item(temp,false);if(left&&!world_->simulation().spawn(71,x,y+.5,z,{temp.item,left,temp.damage}))cursor_={temp.item,left,temp.damage};}
    if(window)send(Packet(0x11).var(window));
    save_player();
}
void GameSession::inventory_click(protocol::Reader& r) {
    int window=r.varint(),revision=r.varint(),index=static_cast<std::int16_t>(r.u16()),button=r.bytes(1)[0],mode=r.varint();
    auto skip_hash=[&]() {
        if(!r.bytes(1)[0])return;
        (void)r.varint();(void)r.varint();int count=r.varint();if(count<0||count>128)throw protocol::Error("Invalid hashed components");
        for(int i=0;i<count;++i){(void)r.varint();(void)read_u32(r);}count=r.varint();if(count<0||count>128)throw protocol::Error("Invalid removed components");for(int i=0;i<count;++i)(void)r.varint();
    };
    int changed=r.varint();if(changed<0||changed>128)throw protocol::Error("Invalid changed slots");
    for(int i=0;i<changed;++i){(void)r.u16();skip_hash();}skip_hash();r.require_end();
    if(!alive()||window!=current_window()||revision!=inventory_revision_) {inventory_sync();return;}
    if(advanced_inventory_click(index,button,mode))return;
    if(index==-999&&mode==0) {
        if(cursor_.item) {int count=button==1?1:cursor_.count;if(world_->simulation().spawn(71,x,y+1,z,{cursor_.item,count,cursor_.damage})) {cursor_.count-=count;if(!cursor_.count)cursor_={};}}
        inventory_sync();return;
    }
    if(!container_positions_.empty()){container_click(index,button,mode);return;}
    if(index<0||index>=46){inventory_sync();return;}
    update_recipe();
    if(index==0) {
        if(mode==0&&button<=1)craft_output(false);
        else if(mode==1&&button<=1) {
            const int output=craft_result_.item;
            for(int crafts=0;crafts<64&&output&&craft_result_.item==output;++crafts) {
                if(!craft_output(true))break;
                update_recipe();
            }
        }
        inventory_sync();return;
    }
    ItemStack* target=nullptr;int inv=-1;
    if(crafting_open_) {if(index<=9)target=&crafting_[index-1];else {inv=index-1;target=&inventory_[inv];}}
    else if(index<=4)target=&crafting_[index-1];else {inv=index;target=&inventory_[inv];}
    auto& dst=*target;
    if(mode==0&&button<=1) {
        if(!cursor_.item) {int count=button==1?(dst.count+1)/2:dst.count;cursor_={dst.item,count,dst.damage};dst.count-=count;if(!dst.count)dst={};}
        else if(!dst.item||same_item(dst,cursor_)) {
            const int capacity=menu_limit(index,cursor_)-dst.count;int moved=std::min(capacity,button==1?1:cursor_.count);
            if(moved>0) {dst={cursor_.item,dst.count+moved,cursor_.damage};cursor_.count-=moved;if(!cursor_.count)cursor_={};}
        } else if(cursor_.count<=menu_limit(index,cursor_))std::swap(dst,cursor_);
    } else if(mode==2&&(button<9||button==40)) {
        const int other=button==40?45:36+button;auto& stack=inventory_[other];
        if(inv!=other&&(!stack.item||stack.count<=menu_limit(index,stack)))std::swap(dst,stack);
    }
    else if(mode==1&&inv>=9&&inv<=44&&dst.item) {
        const int from=inv>=36?9:36,to=inv>=36?36:45;
        for(int pass=0;pass<2;++pass)for(int i=from;i<to&&dst.item;++i) {
            auto& d=inventory_[i];if((pass==0&&d.item&&same_item(d,dst))||(pass==1&&!d.item)) {int count=std::min(dst.count,simdata::item(dst.item)->stack-d.count);d={dst.item,d.count+count,dst.damage};dst.count-=count;if(!dst.count)dst={};}
        }
    } else if(mode==4&&button<=1&&dst.item) {
        int count=button?dst.count:1;if(world_->simulation().spawn(71,x,y+1,z,{dst.item,count,dst.damage})){dst.count-=count;if(!dst.count)dst={};}
    }
    inventory_sync();
}
}
