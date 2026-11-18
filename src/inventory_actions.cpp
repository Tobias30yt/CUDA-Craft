#include "cudacraft/game.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/smelting_data.hpp"
#include <algorithm>

namespace cudacraft {
namespace { bool same(ItemStack a,ItemStack b){return a.item==b.item&&a.damage==b.damage;} }
int GameSession::menu_size() const {
    return container_positions_.empty()?46:static_cast<int>(container_positions_.size())*Simulation::container_slots(container_type_)+36;
}
ItemStack* GameSession::menu_stack(int index) {
    if(index<0||index>=menu_size())return nullptr;
    if(!container_positions_.empty()) {
        const int per=Simulation::container_slots(container_type_),slots=static_cast<int>(container_positions_.size())*per;
        if(index>=slots)return &inventory_[9+index-slots];
        auto* box=world_->simulation().container(container_positions_[index/per]);
        return box?&box->items[index%per]:nullptr;
    }
    if(index==0)return nullptr; // Recipe output is derived, never a stored stack.
    if(crafting_open_)return index<=9?&crafting_[index-1]:&inventory_[index-1];
    return index<=4?&crafting_[index-1]:&inventory_[index];
}
int GameSession::menu_limit(int index,ItemStack stack) const {
    const auto* item=simdata::item(stack.item);if(!item||index<0||index>=menu_size())return 0;
    if(!container_positions_.empty()) {
        if(Simulation::furnace_kind(container_type_)) {
            if(index==2)return 0;
            if(index==1)return stack.item==simdata::bucket_item?1:smelting::fuel(stack.item)>0?item->stack:0;
        }
        return item->stack;
    }
    if(index==0)return 0;
    if(!crafting_open_&&index>=5&&index<=8) {
        const std::string_view name=item->name;
        const bool valid=index==5?(name.ends_with("_helmet")||name.ends_with("_head")||name.ends_with("_skull")||name=="carved_pumpkin"):
            index==6?(name.ends_with("_chestplate")||name=="elytra"):index==7?name.ends_with("_leggings"):name.ends_with("_boots");
        return valid?1:0;
    }
    return item->stack;
}
void GameSession::finish_inventory_action() {
    world_->mark_dirty();
    if(container_positions_.empty()){inventory_sync();return;}
    const auto positions=container_positions_;
    for(auto* peer:world_->players)if(peer->container_positions_==positions)peer->inventory_sync();
}
bool GameSession::advanced_inventory_click(int index,int button,int mode) {
    if(mode!=5&&drag_type_!=-1){reset_drag();inventory_sync();return true;}
    if(mode!=5&&mode!=6) {
        return false;
    }
    if(!container_positions_.empty()&&!container_valid()){close_crafting();inventory_sync();return true;}
    if(mode==5) {
        const int stage=button&3,type=(button>>2)&3;
        if(button>10||stage>2||type>2||(type==2&&survival())||!cursor_.item){reset_drag();inventory_sync();return true;}
        if(stage==0) {
            if(drag_type_!=-1){reset_drag();inventory_sync();return true;}
            drag_type_=type;drag_slots_.clear();return true;
        }
        if(drag_type_!=type){reset_drag();inventory_sync();return true;}
        if(stage==1) {
            auto* slot=menu_stack(index);
            if(slot&&(!slot->item||same(*slot,cursor_))&&slot->count<menu_limit(index,cursor_)&&(type==2||cursor_.count>static_cast<int>(drag_slots_.size())))drag_slots_.insert(index);
            return true;
        }
        const auto selected=drag_slots_;reset_drag();
        if(!selected.empty()) {
            const ItemStack source=cursor_;
            const int share=type==0?source.count/static_cast<int>(selected.size()):type==1?1:simdata::item(source.item)->stack;
            int remaining=source.count;
            // Vanilla handles a single selected slot as an ordinary click; a middle
            // button drag onto only one slot therefore does not fill it.
            if(type!=2||selected.size()>1)for(int i:selected) {
                auto* slot=menu_stack(i);if(!slot|| (slot->item&&!same(*slot,source)))continue;
                const int moved=std::max(0,std::min(share,menu_limit(i,source)-slot->count));
                if(moved){*slot={source.item,slot->count+moved,source.damage};remaining-=moved;}
            }
            cursor_=remaining>0?ItemStack{source.item,remaining,source.damage}:ItemStack{};
        }
        finish_inventory_action();return true;
    }
    reset_drag();
    auto* clicked=menu_stack(index);
    if(button>1||!clicked||clicked->item||!cursor_.item){inventory_sync();return true;}
    const int limit=simdata::item(cursor_.item)->stack;
    // Pick up partial stacks first, then full stacks, in the requested direction.
    for(int pass=0;pass<2&&cursor_.count<limit;++pass)for(int step=0;step<menu_size()&&cursor_.count<limit;++step) {
        const int i=button?menu_size()-1-step:step;auto* slot=menu_stack(i);
        if(!slot||!slot->item||!same(*slot,cursor_)||(pass==0&&slot->count==limit))continue;
        const int moved=std::min(slot->count,limit-cursor_.count);cursor_.count+=moved;slot->count-=moved;if(!slot->count)*slot={};
    }
    finish_inventory_action();return true;
}
}
