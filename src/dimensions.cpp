#include "cudacraft/world.hpp"
#include "cudacraft/game.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/terrain_math.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace cudacraft {
namespace {
int block_state(std::string_view name){for(const auto& b:simdata::blocks)if(name==b.name)return b.state;return 0;}
}
bool GameSession::use_portal_item(BlockPos clicked,int face,int slot) {
    const auto held=inventory_[slot];const auto* item=simdata::item(held.item);if(!item)return false;
    const std::string_view item_name=item->name;
    if(item_name=="bone_meal") {if(world_->simulation().fertilize(clicked)&&survival())set_slot(slot,held.item,held.count-1);return true;}
    if(item_name.ends_with("_hoe")) {
        const std::string_view name=simdata::block(world_->block(clicked)).name;
        if(face!=0&&(name=="dirt"||name=="grass_block"||name=="dirt_path")&&world_->block({clicked.x,clicked.y+1,clicked.z})==0&&world_->set(clicked,block_state("farmland"))&&survival()) {
            const int damage=held.damage+1;set_slot(slot,damage>=item->durability?0:held.item,1,damage>=item->durability?0:damage);
        }
        return true;
    }
    const char* crop=item_name=="wheat_seeds"?"wheat":item_name=="carrot"?"carrots":item_name=="potato"?"potatoes":item_name=="beetroot_seeds"?"beetroots":item_name=="nether_wart"?"nether_wart":nullptr;
    if(crop&&face==1) {
        const std::string_view soil=simdata::block(world_->block(clicked)).name;BlockPos plant{clicked.x,clicked.y+1,clicked.z};
        if(soil==(item_name=="nether_wart"?"soul_sand":"farmland")&&world_->block(plant)==0&&world_->set(plant,block_state(crop))&&survival())set_slot(slot,held.item,held.count-1);
        return true;
    }
    if(std::string_view(item->name)=="flint_and_steel") {
        if(world_->dimension_id()==2)return true;
        constexpr BlockPos directions[]={{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
        auto d=directions[face];BlockPos point{clicked.x+d.x,clicked.y+d.y,clicked.z+d.z};
        const int obsidian=block_state("obsidian");
        auto empty=[&](BlockPos p){int s=world_->block(p);return s==0||std::string_view(simdata::block(s).name)=="fire";};
        if(!empty(point))return true;
        for(int axis=0;axis<2;++axis) {
            auto at=[&](int u,int v){return BlockPos{point.x+(axis==0?u:0),point.y+v,point.z+(axis==1?u:0)};};
            int bottom=0;while(bottom> -21&&empty(at(0,bottom-1)))--bottom;
            if(world_->block(at(0,bottom-1))!=obsidian)continue;
            int left=0;while(left> -21&&empty(at(left-1,bottom)))--left;
            if(world_->block(at(left-1,bottom))!=obsidian)continue;
            int width=0;while(width<=21&&empty(at(left+width,bottom)))++width;
            if(width<2||width>21||world_->block(at(left+width,bottom))!=obsidian)continue;
            bool valid=true;for(int u=0;u<width;++u)valid&=world_->block(at(left+u,bottom-1))==obsidian;
            int height=0;
            for(;height<=21&&valid;++height) {
                bool top=true;for(int u=0;u<width;++u)top&=world_->block(at(left+u,bottom+height))==obsidian;
                if(top)break;
                valid&=world_->block(at(left-1,bottom+height))==obsidian&&world_->block(at(left+width,bottom+height))==obsidian;
                for(int u=0;u<width;++u)valid&=empty(at(left+u,bottom+height));
            }
            if(!valid||height<3||height>21)continue;
            std::vector<std::pair<BlockPos,int>> blocks;
            const int portal=simdata::with_property(block_state("nether_portal"),"axis",axis==0?"x":"z");
            for(int v=0;v<height;++v)for(int u=0;u<width;++u)blocks.emplace_back(at(left+u,bottom+v),portal);
            if(world_->set_blocks(blocks)&&survival()){const int damage=held.damage+1;set_slot(slot,damage>=item->durability?0:held.item,1,damage>=item->durability?0:damage);}
            return true;
        }
        return true;
    }
    if(std::string_view(item->name)=="ender_eye"&&std::string_view(simdata::block(world_->block(clicked)).name)=="end_portal_frame") {
        int frame=world_->block(clicked);if(simdata::property_value(frame,"eye")=="true")return true;
        if(!world_->set(clicked,simdata::with_property(frame,"eye","true")))return true;
        if(survival())set_slot(slot,held.item,held.count-1);
        for(int dx=-2;dx<=2;++dx)for(int dz=-2;dz<=2;++dz) {
            BlockPos center{clicked.x+dx,clicked.y,clicked.z+dz};bool complete=true;
            for(int edge=-1;edge<=1;++edge)for(int side=0;side<4;++side) {
                BlockPos p{center.x+(side<2?(side==0?-2:2):edge),center.y,center.z+(side>=2?(side==2?-2:2):edge)};
                int s=world_->block(p);const char* facing=side==0?"east":side==1?"west":side==2?"south":"north";
                complete&=std::string_view(simdata::block(s).name)=="end_portal_frame"&&simdata::property_value(s,"eye")=="true"&&simdata::property_value(s,"facing")==facing;
            }
            if(complete){std::vector<std::pair<BlockPos,int>> blocks;for(int ox=-1;ox<=1;++ox)for(int oz=-1;oz<=1;++oz)blocks.emplace_back(BlockPos{center.x+ox,center.y,center.z+oz},block_state("end_portal"));world_->set_blocks(blocks);return true;}
        }
        return true;
    }
    return false;
}
World& World::dimension(int id) {
    if(id<0||id>2)throw std::runtime_error("Unknown dimension");
    if(parent_)return parent_->dimension(id);
    if(!id)return *this;
    if(auto it=dimensions_.find(id);it!=dimensions_.end())return *it->second;
    TerrainOptions options=options_;options.generator=id==1?"nether":"end";options.explicit_generator=true;options.explicit_seed=true;
    auto child=std::make_unique<World>(directory_.empty()?std::filesystem::path{}:directory_/(id==1?"DIM-1":"DIM1"),options,this,id);
    // Never rewind a loaded dimension: persisted entity birth times and scheduled
    // updates must remain valid even if it was one step ahead during a transfer.
    child->simulation().tick=std::max(child->simulation().tick,simulation().tick);
    return *dimensions_.emplace(id,std::move(child)).first->second;
}
std::vector<GameSession*> World::all_players() {
    if(parent_)return parent_->all_players();
    auto result=players;for(auto& [id,world]:dimensions_)result.insert(result.end(),world->players.begin(),world->players.end());return result;
}
void GameSession::change_dimension(int id) {
    if(id==world_->dimension_id())return;
    // Construct/load the destination before changing the connected session.
    World& destination=world_->root().dimension(id);
    close_crafting();save_player();
    std::erase(world_->players,this);
    for(auto* p:world_->players){p->send(Packet(0x4b).var(1).var(entity_id));p->send(Packet(0x43).var(1).raw(profile.uuid));}
    world_=&destination;world_->players.push_back(this);
    loaded_.clear();pending_.clear();refresh_.clear();seen_mobs_.clear();center_x_=center_z_=100000;last_mob_sync_=~std::uint64_t{0};
    portal_ticks_=0;portal_cooldown_=world_->simulation().tick+100;attack_after_=eat_at_=0;hurt_until_=world_->simulation().tick+40;
    send(Packet(0x50).var(world_->dimension_type()).str(world_->dimension_name()).u64(world_->seed_hash()).u8(static_cast<std::uint8_t>(mode_)).u8(static_cast<std::uint8_t>(mode_)).u8(0).u8(world_->options().generator=="flat").u8(0).var(0).var(id==1?32:63).u8(3));
    teleport_home();send(Packet(0x26).u8(13).f32(0));
    send(Packet(0x3e).u8(mode_==1?0x0d:0).f32(.05f).f32(.1f));
    for(auto* p:world_->players){player_info(*p);if(p!=this){spawn(*p);p->player_info(*this);p->spawn(*this);}}
    health_sync();inventory_sync();save_player();
    tell(std::string("Entered ")+world_->dimension_name()+". Dimension terrain is experimental.");
}
TerrainChunk generate_dimension(std::int64_t seed,int cx,int cz,int dimension) {
    using namespace terrain_math;
    auto state=[](const char* name){return World::item_state(simdata::item_id(name));};
    static const int rack=state("netherrack"),end=state("end_stone"),bedrock=state("bedrock"),obsidian=state("obsidian"),soul=state("soul_sand"),basalt=state("basalt");
    static const int nether_portal=simdata::with_property(block_state("nether_portal"),"axis","z"),end_portal=block_state("end_portal");
    TerrainChunk result;result.blocks.resize(16*384*16);
    // Independent deterministic prototype, not seed-parity vanilla worldgen.
    result.biomes.fill(dimension==1?34:56); // Pinned 1.21.11 nether_wastes / the_end.
    for(int z=0;z<16;++z)for(int x=0;x<16;++x) {
        const int wx=cx*16+x,wz=cz*16+z;const auto s=static_cast<std::uint64_t>(seed);
        const int floor=32+noise(s+501,wx,0,wz,48)*13/32768;
        const int roof=99+noise(s+502,wx,0,wz,64)*14/32768;
        for(int y=0;y<256;++y) {
            int block=0;
            if(dimension==1&&y<128) {
                if(y==0||y==127)block=bedrock;
                else if(y<=floor||y>=roof||noise(s+503,wx,y*2,wz,24)>18000)block=rack;
                else if(y<=31)block=simdata::lava_state;
                if(block==rack&&y==floor)block=noise(s+504,wx,0,wz,80)>13000?soul:noise(s+505,wx,0,wz,80)>15000?basalt:rack;
                if(std::abs(wx)<=4&&std::abs(wz)<=4&&y>=63&&y<=68)block=y==63?rack:0;
                if(wx==3&&wz>=-1&&wz<=2&&y>=63&&y<=67)block=wz==-1||wz==2||y==63||y==67?obsidian:nether_portal;
            } else if(dimension==2) {
                const double radius=std::hypot(static_cast<double>(wx),static_cast<double>(wz));
                const int top=63+noise(s+601,wx,0,wz,32)*7/32768;
                const double thickness=std::max(0.,38-radius*.35);
                if(radius<110&&y<=top&&y>top-thickness)block=end;
                if(wx>=98&&wx<=102&&std::abs(wz)<=2&&y>=48&&y<=52)block=y==48?obsidian:0;
                if(std::abs(wx)<=2&&std::abs(wz)<=2&&y>=63&&y<=67)block=y==63?bedrock:y==64&&std::abs(wx)<=1&&std::abs(wz)<=1?end_portal:0;
            }
            result.blocks[static_cast<std::size_t>((y+64)*256+z*16+x)]=block;
        }
    }
    return result;
}
}
