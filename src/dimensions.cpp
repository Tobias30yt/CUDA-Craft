#include "cudacraft/world.hpp"
#include "cudacraft/game.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/terrain_math.hpp"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <set>

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
        if(std::string_view(simdata::block(world_->block(clicked)).name)=="tnt") {
            if(world_->simulation().ignite_tnt(clicked)&&survival()) {
                const int damage=held.damage+1;
                set_slot(slot,held.item,damage>=item->durability?0:1,damage>=item->durability?0:damage);
            }
            return true;
        }
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
std::optional<PortalFrame> World::portal_frame(BlockPos point) const {
    const int portal=block(point);
    if(std::string_view(simdata::block(portal).name)!="nether_portal")return {};
    const bool along_x=simdata::property_value(portal,"axis")=="x";
    auto at=[&](int u,int v){return BlockPos{point.x+(along_x?u:0),point.y+v,point.z+(along_x?0:u)};};
    int bottom=0,left=0;
    while(bottom>-21&&block(at(0,bottom-1))==portal)--bottom;
    while(left>-21&&block(at(left-1,bottom))==portal)--left;
    int width=0,height=0;
    while(width<=21&&block(at(left+width,bottom))==portal)++width;
    while(height<=21&&block(at(left,bottom+height))==portal)++height;
    if(width<2||width>21||height<3||height>21)return {};
    for(int u=0;u<width;++u) {
        if(block(at(left+u,bottom-1))!=simdata::obsidian_state||block(at(left+u,bottom+height))!=simdata::obsidian_state)return {};
        for(int v=0;v<height;++v)if(block(at(left+u,bottom+v))!=portal)return {};
    }
    for(int v=0;v<height;++v)if(block(at(left-1,bottom+v))!=simdata::obsidian_state||block(at(left+width,bottom+v))!=simdata::obsidian_state)return {};
    return PortalFrame{at(left,bottom),width,height,along_x};
}
std::optional<PortalFrame> World::find_or_create_portal(BlockPos target,bool along_x) {
    if(dimension_==2)return {};
    target.x=std::clamp(target.x,-1022,1021);target.z=std::clamp(target.z,-1022,1021);
    target.y=std::clamp(target.y,min_y()+1,min_y()+height()-1);
    const int radius=dimension_==1?16:128;
    double best=std::numeric_limits<double>::infinity();int best_y=320;
    std::optional<PortalFrame> result;std::set<BlockPos> seen;
    auto consider=[&](BlockPos p) {
        if(std::abs(p.x-target.x)>radius||std::abs(p.z-target.z)>radius||seen.contains(p))return;
        const auto frame=portal_frame(p);if(!frame)return;
        for(int u=0;u<frame->width;++u)for(int v=0;v<frame->height;++v) {
            BlockPos cell{frame->origin.x+(frame->along_x?u:0),frame->origin.y+v,frame->origin.z+(frame->along_x?0:u)};
            seen.insert(cell);
            if(std::abs(cell.x-target.x)>radius||std::abs(cell.z-target.z)>radius)continue;
            const double dx=cell.x-target.x,dy=cell.y-target.y,dz=cell.z-target.z,distance=dx*dx+dy*dy+dz*dz;
            if(distance<best||(distance==best&&cell.y<best_y)){best=distance;best_y=cell.y;result=frame;}
        }
    };
    // All constructed portals are edits; the prototype Nether also has one generated frame.
    for(const auto& [p,s]:edits_)if(std::string_view(simdata::block(s).name)=="nether_portal")consider(p);
    if(dimension_==1)for(int z=0;z<2;++z)for(int y=64;y<=66;++y)consider({3,y,z});
    if(result)return result;

    // Search a bounded 33x33 area for a supported, dry 4x5 frame with clearance
    // on both sides. Keep creation below the Nether roof, unlike existing portals.
    const int ceiling=dimension_==1?123:min_y()+height()-4;
    auto at=[&](BlockPos p,int u,int v,int n){return BlockPos{p.x+(along_x?u:n),p.y+v,p.z+(along_x?n:u)};};
    auto room=[&](BlockPos p) {
        for(int u=-1;u<=2;++u)for(int n=-1;n<=1;++n)for(int v=-1;v<=3;++v) {
            const auto cell=at(p,u,v,n);if(!valid(cell)||cell.y<=min_y()||cell.y>=min_y()+height())return false;
            const int s=block(cell);
            if(v==-1) {if(!simdata::block(s).solid||std::string_view(simdata::block(s).name)=="magma_block")return false;}
            else if(s!=0)return false;
        }
        return true;
    };
    std::optional<BlockPos> origin;best=std::numeric_limits<double>::infinity();
    for(int dx=-16;dx<=16;++dx)for(int dz=-16;dz<=16;++dz) {
        if(target.x+dx< -1022||target.x+dx>1021||target.z+dz< -1022||target.z+dz>1021)continue;
        for(int y=ceiling;y>=min_y()+2;--y) {
            const double distance=dx*dx+dz*dz+static_cast<double>(y-target.y)*(y-target.y);
            if(distance>=best)continue;
            BlockPos p{target.x+dx,y,target.z+dz};
            if(block(p)!=0||!solid({p.x,p.y-1,p.z})||!room(p))continue;
            best=distance;origin=p;
        }
    }
    const bool platform=!origin;
    if(!origin)origin=BlockPos{target.x,std::clamp(target.y,70,ceiling-5),target.z};
    std::map<BlockPos,int> changes;
    if(platform)for(int u=-1;u<=2;++u)for(int n=-1;n<=1;++n)for(int v=-1;v<=3;++v) {
        const auto p=at(*origin,u,v,n);
        // Never carve through a player's solid edits, containers or bedrock for fallback.
        if(auto it=edits_.find(p);it!=edits_.end()&&it->second!=0)return {};
        if(std::string_view(simdata::block(block(p)).name)=="bedrock")return {};
        changes[p]=v==-1?simdata::obsidian_state:0;
    }
    const int portal=simdata::with_property(block_state("nether_portal"),"axis",along_x?"x":"z");
    for(int u=-1;u<=2;++u)for(int v=-1;v<=3;++v)
        changes[at(*origin,u,v,0)]=(u==-1||u==2||v==-1||v==3)?simdata::obsidian_state:portal;
    std::vector<std::pair<BlockPos,int>> batch(changes.begin(),changes.end());
    if(!set_blocks(batch))return {};
    return PortalFrame{*origin,2,3,along_x};
}
void GameSession::change_dimension(int id,bool through_portal) {
    if(id==world_->dimension_id())return;
    // Construct/load the destination before changing the connected session.
    World& destination=world_->root().dimension(id);
    std::optional<PortalFrame> arrival;double arrival_x=0,arrival_y=0,arrival_z=0;float arrival_yaw=yaw;
    if(through_portal) {
        const auto source=world_->portal_frame({static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y)),static_cast<int>(std::floor(z))});
        if(!source||world_->dimension_id()==2||id==2)return;
        const double scale=id==1?0.125:8.;
        const BlockPos target{static_cast<int>(std::floor(std::clamp(x*scale,-1022.,1021.))),static_cast<int>(std::floor(y)),static_cast<int>(std::floor(std::clamp(z*scale,-1022.,1021.)))};
        arrival=destination.find_or_create_portal(target,source->along_x);
        if(!arrival){portal_ticks_=0;portal_cooldown_=world_->simulation().tick+100;tell("No safe destination portal could be created. Travel cancelled.");return;}
        const double fraction=std::clamp(((source->along_x?x-source->origin.x:z-source->origin.z)-.3)/(source->width-.6),0.,1.);
        const double vertical=std::clamp((y-source->origin.y)/(source->height-1.8),0.,1.);
        const double u=.3+fraction*(arrival->width-.6);
        arrival_x=arrival->origin.x+(arrival->along_x?u:.5);arrival_z=arrival->origin.z+(arrival->along_x?.5:u);
        arrival_y=arrival->origin.y+vertical*(arrival->height-1.8);
        if(source->along_x!=arrival->along_x)arrival_yaw+=90;
    }
    close_crafting();save_player();
    std::erase(world_->players,this);
    for(auto* p:world_->players){p->send(Packet(0x4b).var(1).var(entity_id));p->send(Packet(0x43).var(1).raw(profile.uuid));}
    world_=&destination;world_->players.push_back(this);
    loaded_.clear();pending_.clear();refresh_.clear();light_refresh_.clear();seen_mobs_.clear();seen_vehicles_.clear();seen_tnt_.clear();center_x_=center_z_=100000;last_mob_sync_=~std::uint64_t{0};
    portal_ticks_=0;portal_cooldown_=world_->simulation().tick+100;attack_after_=eat_at_=0;hurt_until_=world_->simulation().tick+40;
    send(Packet(0x50).var(world_->dimension_type()).str(world_->dimension_name()).u64(world_->seed_hash()).u8(static_cast<std::uint8_t>(mode_)).u8(static_cast<std::uint8_t>(mode_)).u8(0).u8(world_->options().generator=="flat").u8(0).var(0).var(id==1?32:63).u8(3));
    if(arrival) {
        x=arrival_x;y=arrival_y;z=arrival_z;yaw=arrival_yaw;
        ++teleport_;awaiting_teleport_=true;falling_=false;fall_start_=y;mining_active_=false;eat_at_=0;
        send(Packet(0x46).var(teleport_).f64(x).f64(y).f64(z).f64(0).f64(0).f64(0).f32(yaw).f32(pitch).u32(0));update_view();
    } else teleport_home();
    send(Packet(0x26).u8(13).f32(0));
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
