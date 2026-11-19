#include "cudacraft/game.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/smelting_data.hpp"
#include <algorithm>
#include <cmath>

namespace cudacraft {
namespace {
BlockPos offset(BlockPos a,BlockPos b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
BlockPos clockwise(int state){auto f=simdata::property_value(state,"facing");return f=="north"?BlockPos{1,0,0}:f=="east"?BlockPos{0,0,1}:f=="south"?BlockPos{-1,0,0}:BlockPos{0,0,-1};}
BlockPos other_chest(BlockPos p,int state){auto d=clockwise(state);if(simdata::property_value(state,"type")=="right"){d.x=-d.x;d.z=-d.z;}return offset(p,d);}
bool same(ItemStack a,ItemStack b){return a.item==b.item&&a.damage==b.damage;}
}
int Simulation::container_type(int state) {
    const auto& block=simdata::block(state);const std::string_view name=block.name;
    return name=="chest"||name=="trapped_chest"||name=="barrel"||name=="hopper"||name=="dropper"||
        name=="dispenser"||furnace_kind(state)?block.first:0;
}
int Simulation::container_slots(int state) {
    if(furnace_kind(state))return 3;
    const std::string_view name=simdata::block(state).name;
    return name=="hopper"?5:(name=="dropper"||name=="dispenser")?9:27;
}
int Simulation::furnace_kind(int state){std::string_view name=simdata::block(state).name;return name=="furnace"?1:name=="blast_furnace"?2:name=="smoker"?3:0;}
std::vector<BlockPos> Simulation::container_positions(BlockPos p) const {
    int state=world_.block(p);if(!container_type(state))return {};
    auto type=simdata::property_value(state,"type");
    if(type=="left"||type=="right") {
        auto other=other_chest(p,state);int next=world_.block(other);
        if(container_type(next)==container_type(state)&&simdata::property_value(next,"facing")==simdata::property_value(state,"facing")&&simdata::property_value(next,"type")==(type=="left"?"right":"left"))
            return type=="left"?std::vector<BlockPos>{p,other}:std::vector<BlockPos>{other,p};
    }
    return {p};
}
Container* Simulation::container(BlockPos p,bool create) {
    int type=container_type(world_.block(p));if(!type)return nullptr;
    auto found=containers.find(p);
    if(found!=containers.end())return found->second.type==type?&found->second:nullptr;
    if(!create||containers.size()>=4096)return nullptr;
    world_.mark_dirty();return &containers.emplace(p,Container{type,{}}).first->second;
}
std::size_t Simulation::container_drops(BlockPos p,int replacement) const {
    auto found=containers.find(p);if(found==containers.end()||found->second.type==container_type(replacement))return 0;
    return static_cast<std::size_t>(std::count_if(found->second.items.begin(),found->second.items.end(),[](auto item){return item.item!=0;}));
}
void Simulation::container_changed(BlockPos p,int before,int after) {
    const bool removed=container_type(before)!=container_type(after);
    if(removed) {
        auto found=containers.find(p);
        if(found!=containers.end()) {
            // World mutations reserve entity capacity before changing the block.
            auto items=found->second.items;containers.erase(found);
            for(auto item:items)if(item.item)spawn(71,p.x+.5,p.y+.5,p.z+.5,item);
            world_.mark_dirty();
        }
        if(simdata::property_value(before,"type")=="left"||simdata::property_value(before,"type")=="right") {
            auto other=other_chest(p,before);int state=world_.block(other);
            if(container_type(state)==container_type(before)&&other_chest(other,state)==p)
                world_.set(other,simdata::with_property(state,"type","single"));
        }
    }
    const bool layout_changed=removed||simdata::property_value(before,"type")!=simdata::property_value(after,"type")||simdata::property_value(before,"facing")!=simdata::property_value(after,"facing");
    if(layout_changed&&(container_type(before)||container_type(after)))for(auto* player:world_.players)player->container_changed(p,removed);
}
bool GameSession::container_valid() const {
    if(container_positions_.empty()||!alive()||!reachable(container_anchor_))return false;
    if(world_->simulation().container_positions(container_positions_.front())!=container_positions_)return false;
    for(auto p:container_positions_)if(Simulation::container_type(world_->block(p))!=container_type_||!world_->simulation().container(p))return false;
    return true;
}
bool GameSession::open_container(BlockPos p) {
    auto positions=world_->simulation().container_positions(p);if(positions.empty())return false;
    const std::string_view name=simdata::block(world_->block(p)).name;
    if(name=="chest"||name=="trapped_chest")for(auto q:positions)if(world_->solid({q.x,q.y+1,q.z})){tell("The chest lid is blocked.");return true;}
    for(auto q:positions)if(!world_->simulation().container(q,true)){tell("Container storage capacity reached.");return true;}
    close_crafting();container_positions_=std::move(positions);container_anchor_=p;container_type_=Simulation::container_type(world_->block(p));
    container_window_=next_container_window_;next_container_window_=next_container_window_==100?2:next_container_window_+1;
    const int kind=Simulation::furnace_kind(container_type_);
    const int menu=kind==1?14:kind==2?10:kind==3?22:name=="hopper"?16:(name=="dropper"||name=="dispenser")?6:container_positions_.size()==2?5:2;
    const char* title=kind==1?"Furnace":kind==2?"Blast Furnace":kind==3?"Smoker":name=="hopper"?"Item Hopper":
        name=="dropper"?"Dropper":name=="dispenser"?"Dispenser":name=="barrel"?"Barrel":container_positions_.size()==2?"Large Chest":"Chest";
    send(Packet(0x39).var(container_window_).var(menu).text(title));
    inventory_sync();return true;
}
void GameSession::container_changed(BlockPos p,bool removed) {
    if(std::find(container_positions_.begin(),container_positions_.end(),p)==container_positions_.end())return;
    if(removed||!container_valid())close_crafting();
    inventory_sync();
}
void GameSession::container_click(int index,int button,int mode) {
    if(!container_valid()){close_crafting();inventory_sync();return;}
    const int kind=Simulation::furnace_kind(container_type_),per_container=Simulation::container_slots(container_type_);
    const int slots=static_cast<int>(container_positions_.size())*per_container;
    if(index<0||index>=slots+36){inventory_sync();return;}
    auto at=[&](int i)->ItemStack& {if(i<slots)return world_->simulation().container(container_positions_[i/per_container])->items[i%per_container];return inventory_[9+i-slots];};
    auto accepts=[&](int i,ItemStack item){return !item.item||!kind||i>=slots||i==0||(i==1&&(smelting::fuel(item.item)>0||item.item==simdata::bucket_item));};
    auto capacity=[&](int i,ItemStack item){return kind&&i==1&&item.item==simdata::bucket_item?1:simdata::item(item.item)->stack;};
    auto& dst=at(index);
    if(kind&&index==2&&mode==0&&button<=1) {
        if(dst.item&&(!cursor_.item||same(dst,cursor_))) {
            const int count=std::min(simdata::item(dst.item)->stack-cursor_.count,button?(cursor_.item?1:(dst.count+1)/2):dst.count);
            cursor_={dst.item,cursor_.count+count,dst.damage};dst.count-=count;if(!dst.count)dst={};
        }
    } else if(mode==0&&button<=1) {
        if(!cursor_.item){const int count=button?(dst.count+1)/2:dst.count;cursor_={dst.item,count,dst.damage};dst.count-=count;if(!dst.count)dst={};}
        else if(accepts(index,cursor_)) {
            if(!dst.item||same(dst,cursor_)) {const int moved=std::min(capacity(index,cursor_)-dst.count,button?1:cursor_.count);if(moved>0){dst={cursor_.item,dst.count+moved,cursor_.damage};cursor_.count-=moved;if(!cursor_.count)cursor_={};}}
            else if(cursor_.count<=capacity(index,cursor_))std::swap(dst,cursor_);
        }
    } else if(mode==2&&(button<9||button==40)&&!(index>=slots&&9+index-slots==(button==40?45:36+button))) {
        auto& hotbar=inventory_[button==40?45:36+button];
        if(kind&&index==2){if(dst.item&&(!hotbar.item||same(dst,hotbar))){int count=std::min(dst.count,simdata::item(dst.item)->stack-hotbar.count);hotbar={dst.item,hotbar.count+count,dst.damage};dst.count-=count;if(!dst.count)dst={};}}
        else if(accepts(index,hotbar)&&(!hotbar.item||hotbar.count<=capacity(index,hotbar)))std::swap(dst,hotbar);
    }
    else if(mode==1&&dst.item) {
        std::vector<int> targets;
        if(index<slots)for(int i=slots+35;i>=slots;--i)targets.push_back(i);
        else if(!kind)for(int i=0;i<slots;++i)targets.push_back(i);
        else if(smelting::recipe(kind,dst.item))targets.push_back(0);
        else if(smelting::fuel(dst.item)||dst.item==simdata::bucket_item)targets.push_back(1);
        else {const bool hotbar=index>=slots+27;for(int i=hotbar?slots:slots+27;i<(hotbar?slots+27:slots+36);++i)targets.push_back(i);}
        for(int pass=0;pass<2;++pass)for(int target:targets) {
            if(!dst.item)break;auto& next=at(target);
            if((pass==0&&next.item&&same(next,dst))||(pass==1&&!next.item)) {
                int count=std::min(dst.count,capacity(target,dst)-next.count);if(count>0){next={dst.item,next.count+count,dst.damage};dst.count-=count;if(!dst.count)dst={};}
            }
        }
    } else if(mode==4&&button<=1&&dst.item) {
        const int count=button?dst.count:1;if(world_->simulation().spawn(71,x,y+1,z,{dst.item,count,dst.damage})){dst.count-=count;if(!dst.count)dst={};}
    }
    world_->mark_dirty();
    for(const auto bank:container_positions_)world_->simulation().inventory_signal_changed(bank);
    // Each viewer receives its own new revision; stale clicks are resynchronized.
    for(auto* peer:world_->players)if(peer->container_positions_==container_positions_)peer->inventory_sync();
}
void GameSession::container_properties() {
    if(container_positions_.empty()||!Simulation::furnace_kind(container_type_))return;
    auto* container=world_->simulation().container(container_positions_.front());if(!container)return;
    const int values[]={container->burn_left,container->burn_total,container->cook_progress,container->cook_total};
    for(int i=0;i<4;++i)send(Packet(0x13).var(container_window_).u16(static_cast<std::uint16_t>(i)).u16(static_cast<std::uint16_t>(values[i])));
}
void GameSession::container_progress(BlockPos p){if(container_positions_.size()==1&&container_positions_.front()==p)container_properties();}

void Simulation::update_ejectors(BlockPos changed,int before,int after) {
    using namespace simdata;
    if(ejectors_.empty())return;
    if((std::string_view(block(before).name)=="dropper"||std::string_view(block(before).name)=="dispenser")&&
       block(before).first==block(after).first&&
       property_value(before,"facing")==property_value(after,"facing"))return;
    auto update=[&](BlockPos position) {
        if(!ejectors_.contains(position))return;
        const int state=world_.block(position);
        const bool powered=neighbor_power(position,true)>0||neighbor_power(offset(position,{0,1,0}),true)>0;
        if(powered==(property_value(state,"triggered")=="true"))return;
        const int next=with_property(state,"triggered",powered?"true":"false");
        if(world_.set(position,next)&&powered&&devices_.size()<65536)
            devices_.try_emplace(position,Event{tick+4,next});
    };
    constexpr BlockPos adjacent[]={{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
    for(const auto source:{changed,offset(changed,{0,-1,0})}) {
        update(source);
        for(auto direction:adjacent) {
            const auto neighbor=offset(source,direction);
            update(neighbor);
            for(auto second:adjacent)update(offset(neighbor,second));
        }
    }
}

void Simulation::dispense_container(BlockPos position,int state) {
    using namespace simdata;
    auto* source=container(position);
    if(!source)return;
    std::array<int,9> occupied{};
    std::size_t count=0;
    for(int slot=0;slot<9;++slot)if(source->items[slot].item)occupied[count++]=slot;
    if(!count)return;
    auto random=static_cast<std::uint64_t>(world_.options().seed)^packed_position(position)^(tick*0x9e3779b97f4a7c15ULL);
    random^=random>>30; random*=0xbf58476d1ce4e5b9ULL; random^=random>>27;
    auto& item_stack=source->items[occupied[random%count]];
    const auto direction=property_value(state,"facing");
    const BlockPos facing=direction=="up"?BlockPos{0,1,0}:direction=="down"?BlockPos{0,-1,0}:
        direction=="north"?BlockPos{0,0,-1}:direction=="south"?BlockPos{0,0,1}:
        direction=="west"?BlockPos{-1,0,0}:BlockPos{1,0,0};
    const auto target=offset(position,facing);
    auto notify=[&](BlockPos p) {
        inventory_signal_changed(p);
        for(auto* player:world_.players)player->container_changed(p);
    };
    bool transferred=false;
    if(std::string_view(block(state).name)=="dispenser") {
        const std::string_view item_name=item(item_stack.item)->name;
        const auto below=offset(target,{0,-1,0});
        const int in_front=world_.block(target);
        const int under_front=world_.block(below);
        Vehicle vehicle;
        if(item_name=="chest_minecart"||item_name=="hopper_minecart") {
            auto is_rail=[](int block_state) {
                const std::string_view name=block(block_state).name;
                return name=="rail"||name=="detector_rail"||name=="powered_rail"||name=="activator_rail";
            };
            std::optional<double> height;
            if(is_rail(in_front))
                height=property_value(in_front,"shape").starts_with("ascending_")?.6:.1;
            else if(in_front==0&&is_rail(under_front))
                height=facing.y!=-1&&property_value(under_front,"shape").starts_with("ascending_")?-.4:-.9;
            if(height) {
                vehicle.type=item_name=="chest_minecart"?25:65;
                vehicle.x=position.x+.5+facing.x*1.125;
                vehicle.y=position.y+facing.y+*height;
                vehicle.z=position.z+.5+facing.z*1.125;
            }
        } else if(item_name=="oak_boat") {
            auto water_at=[](int block_state){return water(block_state)||property_value(block_state,"waterlogged")=="true";};
            if(water_at(in_front)||(in_front==0&&water_at(under_front))) {
                vehicle.type=89;
                vehicle.x=position.x+.5+facing.x*1.25;
                vehicle.y=position.y+.5+facing.y*1.125+static_cast<int>(water_at(in_front));
                vehicle.z=position.z+.5+facing.z*1.25;
                vehicle.yaw=facing.x==1?270.f:facing.x==-1?90.f:facing.z==-1?180.f:0.f;
            }
        }
        if(vehicle.type)transferred=spawn_vehicle(vehicle)!=0;
        else transferred=spawn(71,target.x+.5,target.y+.5,target.z+.5,{item_stack.item,1,item_stack.damage})!=0;
    } else if(container_type(world_.block(target))) {
        for(const auto bank:container_positions(target)) {
            auto* receiver=container(bank,true);
            if(!receiver||furnace_kind(receiver->type))continue;
            for(int slot=0;slot<container_slots(receiver->type);++slot) {
                auto& destination=receiver->items[slot];
                if(destination.item&&(!same(destination,item_stack)||destination.count>=item(item_stack.item)->stack))continue;
                destination={item_stack.item,destination.count+1,item_stack.damage};
                notify(bank);
                transferred=true;
                break;
            }
            if(transferred)break;
        }
    } else {
        transferred=spawn(71,target.x+.5,target.y+.5,target.z+.5,{item_stack.item,1,item_stack.damage})!=0;
    }
    if(!transferred)return;
    if(!--item_stack.count)item_stack={};
    notify(position);
    world_.mark_dirty();
}

void Simulation::tick_hoppers() {
    using namespace simdata;
    auto changed=[&](BlockPos position) {
        world_.mark_dirty();
        inventory_signal_changed(position);
        for(auto* player:world_.players)player->container_changed(position);
    };
    auto transfer=[&](BlockPos from,BlockPos to) {
        if(from==to)return false;
        auto* source=container(from);
        if(!source||furnace_kind(source->type))return false; // Sided furnace slots are separate.
        const int target_type=container_type(world_.block(to));
        if(!target_type||furnace_kind(target_type))return false;
        for(int i=0;i<container_slots(source->type);++i) {
            auto& item=source->items[i];
            if(!item.item)continue;
            auto* target=container(to,true);
            if(!target)return false;
            const auto limit=simdata::item(item.item)->stack;
            const int slots=container_slots(target->type);
            const bool was_empty=std::none_of(target->items.begin(),target->items.begin()+slots,
                                              [](const auto& stack){return stack.item!=0;});
            for(int slot=0;slot<slots;++slot) {
                auto& destination=target->items[slot];
                if(destination.item&&(!same(destination,item)||destination.count>=limit))continue;
                destination={item.item,destination.count+1,item.damage};
                if(!--item.count)item={};
                if(was_empty&&std::string_view(block(target->type).name)=="hopper"&&target->transfer_cooldown<=8)
                    target->transfer_cooldown=target->last_transfer_tick==tick?7:8;
                changed(from);
                changed(to);
                return true;
            }
        }
        return false;
    };
    for(auto p:hoppers_) {
        int state=world_.block(p);
        const bool enabled=neighbor_power(p,true)==0;
        const int next=with_property(state,"enabled",enabled?"true":"false");
        if(next!=state)world_.set(p,next);
        auto* hopper=container(p);
        if(hopper) {
            hopper->last_transfer_tick=tick;
            if(hopper->transfer_cooldown>0) {
                --hopper->transfer_cooldown;
                world_.mark_dirty();
            }
            if(hopper->transfer_cooldown>0)continue;
        }
        if(!enabled)continue;
        const auto facing=property_value(state,"facing");
        const BlockPos direction=facing=="north"?BlockPos{0,0,-1}:facing=="south"?BlockPos{0,0,1}:
            facing=="west"?BlockPos{-1,0,0}:facing=="east"?BlockPos{1,0,0}:BlockPos{0,-1,0};
        bool moved=transfer(p,offset(p,direction));
        const auto source=offset(p,{0,1,0});
        for(const auto bank:container_positions(source)) {
            if(transfer(bank,p)) { moved=true; break; }
        }
        if(moved) {
            auto* current=container(p);
            current->transfer_cooldown=8;
            current->last_transfer_tick=tick;
            world_.mark_dirty();
        }
    }
}

void Simulation::tick_furnaces() {
    for(auto& [p,c]:containers) {
        const int kind=furnace_kind(c.type);if(!kind)continue;
        const auto before=c;auto& input=c.items[0];auto& fuel=c.items[1];auto& output=c.items[2];
        const auto* recipe=smelting::recipe(kind,input.item);
        if(c.cook_input!=input.item){c.cook_input=input.item;c.cook_progress=0;}
        c.cook_total=recipe?recipe->ticks:kind==1?200:100;
        const bool can_cook=recipe&&input.count>0&&(!output.item||(output.item==recipe->output&&output.damage==0))&&output.count+recipe->count<=simdata::item(recipe->output)->stack;
        if(c.burn_left>0)--c.burn_left;
        if(c.burn_left==0&&can_cook) {
            const int duration=smelting::fuel(fuel.item)/(kind==1?1:2);
            if(duration>0&&fuel.count>0) {
                c.burn_left=c.burn_total=duration;
                const bool lava=fuel.item==simdata::item_id("lava_bucket");
                if(--fuel.count==0)fuel=lava?ItemStack{simdata::bucket_item,1,0}:ItemStack{};
            }
        }
        if(c.burn_left>0&&can_cook) {
            if(++c.cook_progress>=c.cook_total) {
                output={recipe->output,output.count+recipe->count,0};
                if(input.item==simdata::item_id("wet_sponge")&&fuel.item==simdata::bucket_item)fuel={simdata::water_bucket_item,1,0};
                if(--input.count==0)input={};c.cook_progress=0;
            }
        } else if(c.burn_left>0)c.cook_progress=0;
        else c.cook_progress=std::max(0,c.cook_progress-2);
        const int state=world_.block(p),lit=simdata::with_property(state,"lit",c.burn_left>0?"true":"false");
        if(state!=lit)world_.set(p,lit);
        const bool items_changed=before.items!=c.items;
        const bool progress_changed=before.burn_left!=c.burn_left||before.burn_total!=c.burn_total||before.cook_progress!=c.cook_progress||before.cook_total!=c.cook_total||before.cook_input!=c.cook_input;
        if(items_changed||progress_changed) {
            world_.mark_dirty();
            if(items_changed)inventory_signal_changed(p);
            for(auto* player:world_.players) {if(items_changed)player->container_changed(p);else player->container_progress(p);}
        }
    }
}
}
