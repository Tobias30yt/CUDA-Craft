#include "cudacraft/world.hpp"
#include "cudacraft/world_lock.hpp"
#include "cudacraft/game.hpp"
#include "cudacraft/gpu.hpp"
#include "cudacraft/generated_game_data.hpp"
#include "cudacraft/terrain_math.hpp"
#include "cudacraft/biome_source.hpp"
#include "cudacraft/noise_terrain.hpp"
#include "cudacraft/simulation.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/admin.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <array>
#include <bit>
#include <fstream>
#include <iostream>
#include <charconv>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace cudacraft {
World::World(std::filesystem::path directory,TerrainOptions options,World* parent,int dimension) : parent_(parent),dimension_(dimension),directory_(std::move(directory)), options_(std::move(options)) {
    if(!directory_.empty()) lock_=std::make_unique<WorldLock>(directory_);
    simulation_=std::make_unique<Simulation>(*this);
    if(!parent_)admin_=std::make_unique<Administration>(*this,directory_);
    if(options_.generator!="flat" && options_.generator!="overworld" && options_.generator!="climate" && options_.generator!="density" && options_.generator!="nether" && options_.generator!="end") throw std::runtime_error("Unknown generator");
    const auto file=directory_.empty()?std::filesystem::path{}:directory_/"blocks.json";
    if (directory_.empty() || !std::filesystem::exists(file)) {
        if(options_.generator=="flat") base_=gpu::flat_terrain();
        if(options_.generator=="climate" || options_.generator=="density") biome_source_=std::make_unique<BiomeSource>(options_.seed);
        if(options_.generator=="density") noise_terrain_=std::make_unique<NoiseTerrain>(*biome_source_);
        if(!directory_.empty()) { std::filesystem::create_directories(directory_); dirty_=true; save(); }
        return;
    }
    if (std::filesystem::file_size(file)>32*1024*1024) throw std::runtime_error("World edit file exceeds size limit");
    std::ifstream input(file);
    auto document=nlohmann::json::parse(input);
    const int format=document.at("format").get<int>();
    if ((format!=1 && format!=2) || document.at("protocol")!=774) throw std::runtime_error("Unsupported world edit format/version");
    const auto identity=document.at("generator").get<std::string>();
    const std::string generator=identity=="cuda-craft-flat-v1"?"flat":identity=="cuda-craft-overworld-v1"?"overworld":identity=="cuda-craft-climate-v1"?"climate":identity=="cuda-craft-density-v1"?"density":identity=="cuda-craft-nether-v1"?"nether":identity=="cuda-craft-end-v1"?"end":"";
    if(generator.empty() || (format==1 && generator!="flat")) throw std::runtime_error("Unsupported saved generator identity");
    std::int64_t saved_seed=0;
    if(format==2) {
        const auto seed_text=document.at("seed").dump();
        const auto parsed=std::from_chars(seed_text.data(),seed_text.data()+seed_text.size(),saved_seed);
        if(parsed.ec!=std::errc{} || parsed.ptr!=seed_text.data()+seed_text.size()) throw std::runtime_error("Invalid saved seed");
    }
    if((options_.explicit_generator && options_.generator!=generator) || (options_.explicit_seed && options_.seed!=saved_seed))
        throw std::runtime_error("World seed/generator differs from saved world. Choose a NEW --world directory; existing builds were not changed.");
    options_.generator=generator; options_.seed=saved_seed;
    if(generator=="flat") base_=gpu::flat_terrain();
    if(generator=="climate" || generator=="density") biome_source_=std::make_unique<BiomeSource>(options_.seed);
    if(generator=="density") noise_terrain_=std::make_unique<NoiseTerrain>(*biome_source_);
    const auto& edits=document.at("edits");
    if (!edits.is_array() || edits.size()>100000) throw std::runtime_error("World edit limit exceeded");
    for (const auto& e:edits) {
        if (!e.is_array() || e.size()!=4) throw std::runtime_error("Invalid world edit");
        BlockPos p{e[0].get<int>(),e[1].get<int>(),e[2].get<int>()};
        const int state=e[3].get<int>();
        if (!valid(p) || p.y<=min_y()||p.y>=min_y()+height() || !simdata::valid_state(state)) throw std::runtime_error("Invalid saved block state");
        edits_[p]=state;
    }
    for(const auto& [p,state]:edits_)simulation_->changed(p,0,state,false);
    if(document.contains("simulation")) simulation_->load(document.at("simulation").dump());
    if(!parent_)for(int id:{1,2})if(std::filesystem::exists(directory_/(id==1?"DIM-1":"DIM1")/"blocks.json"))this->dimension(id);
}
World::~World() { try { save(); } catch(const std::exception& e) { std::cerr<<"World save failed: "<<e.what()<<'\n'; } }
bool World::valid(BlockPos p) { return p.x>=-1024 && p.x<1024 && p.z>=-1024 && p.z<1024 && p.y>=-64 && p.y<320; }
int World::item_state(int item) {
    if(item<=0)return 0;
    for (const auto& b:game_data::building_blocks) if (b.item==item) return b.state;
    if(item==simdata::redstone_item)return simdata::redstone_wire_state;
    if(item==322)return 3169; // 1.21.11 standing torch.
    for(int state:{simdata::lever_state,simdata::redstone_torch_state,simdata::repeater_state,simdata::redstone_lamp_state,simdata::redstone_block_state,simdata::crafting_table_state})
        if(simdata::block(state).item==item)return state;
    for(const auto& b:simdata::blocks)if(b.item==item)return b.state;
    return 0;
}
int World::state_item(int state) {
    for (const auto& b:game_data::building_blocks) if (b.state==state) return b.item;
    if(simdata::wire(state))return simdata::redstone_item;
    return simdata::block(state).item;
}
int World::block(BlockPos p) const {
    if (!valid(p)||p.y<min_y()||p.y>=min_y()+height()) return 0;
    const auto found=edits_.find(p);
    if(found!=edits_.end()) return found->second;
    return base_block(p);
}
const TerrainChunk& World::terrain(int cx,int cz) const {
    const auto key=std::pair(cx,cz);
    auto found=terrain_cache_.find(key);
    if(found!=terrain_cache_.end()) { ++performance.cache_hits;found->second.used=++cache_clock_; return found->second.terrain; }
    if(terrain_cache_.size()>=256) {
        const auto oldest=std::min_element(terrain_cache_.begin(),terrain_cache_.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
        terrain_cache_.erase(oldest);
    }
    const auto generation_start=Performance::Clock::now();
    auto generated=dimension_?generate_dimension(options_.seed,cx,cz,dimension_):noise_terrain_?noise_terrain_->decorated_chunk(cx,cz):biome_source_?generate_climate_overworld(*biome_source_,cx,cz):generate_overworld(options_.seed,cx,cz);
    ++performance.chunks_generated;performance.last_generation_ms=Performance::elapsed(generation_start);performance.max_generation_ms=std::max(performance.max_generation_ms,performance.last_generation_ms);
    return terrain_cache_.emplace(key,CachedChunk{std::move(generated),++cache_clock_}).first->second.terrain;
}
std::uint64_t World::seed_hash() const { return biome_source_?biome_source_->seed_hash():0; }
int World::base_block(BlockPos p) const {
    const auto index=static_cast<std::size_t>((p.y+64)*256+(p.z&15)*16+(p.x&15));
    if(options_.generator=="flat") return static_cast<int>(base_[index]);
    return static_cast<int>(terrain(terrain_math::floor_div(p.x,16),terrain_math::floor_div(p.z,16)).blocks[index]);
}
int World::biome(int x,int y,int z) const {
    if(options_.generator=="flat") return game_data::plains_biome;
    return terrain(terrain_math::floor_div(x,16),terrain_math::floor_div(z,16)).biomes[
        static_cast<std::size_t>((std::clamp(y,-64,319)+64)/4*16+((z&15)/4)*4+(x&15)/4)];
}
BlockPos World::spawn() const {
    if(dimension_==1)return {0,64,0};
    if(dimension_==2)return {100,49,0};
    using namespace terrain_ids;
    int sx=0,sz=0;
    if(options_.generator!="flat") {
        bool found=false;
        for(int radius=0;radius<=512 && !found;radius+=16) for(int z=-radius;z<=radius && !found;z+=16) for(int x=-radius;x<=radius;++x) {
            if(x%16 || std::max(std::abs(x),std::abs(z))!=radius) continue;
            const auto c=noise_terrain_?noise_terrain_->column(x,z):biome_source_?biome_source_->column(x,z):terrain_math::column(static_cast<std::uint64_t>(options_.seed),x,z);
            if(c.height>=65 && c.height<120) { sx=x;sz=z;found=true;break; }
        }
    }
    for(int y=319;y>=-64;--y) {
        const int state=block({sx,y,sz});
        if(state!=air && state!=water && state!=lava && state!=short_grass && state!=dandelion && state!=poppy)
            return {sx,y+1,sz};
    }
    return {sx,64,sz};
}
bool World::set(BlockPos p,int state) {
    if(!valid(p) || p.y<=min_y()||p.y>=min_y()+height() || !simdata::valid_state(state)) return false;
    const int previous=block(p);
    if(previous==state) return true;
    const auto drops=simulation_->container_drops(p,state);
    if(drops&&simulation_->mobs.size()+drops+1>4096)return false;
    if(edits_.size()>=100000 && !edits_.contains(p)) return false;
    const int base=base_block(p);
    if (state==base) edits_.erase(p); else edits_[p]=state;
    dirty_=true;
    simulation_->changed(p,previous,state);
    for(auto* player:players) player->block_changed(p,state);
    queue_bulb_light(p,previous,state);
    simulation_->update_copper_bulbs(p,previous,state);
    return true;
}
bool World::set_blocks(const std::vector<std::pair<BlockPos,int>>& blocks) {
    struct Change {BlockPos p;int before,after,base;};
    std::vector<Change> changes;std::set<BlockPos> unique;auto count=edits_.size();std::size_t drops=0;
    for(const auto& [p,state]:blocks) {
        if(!valid(p)||p.y<=min_y()||p.y>=min_y()+height()||!simdata::valid_state(state)||!unique.insert(p).second)return false;
        const int base=base_block(p);const bool exists=edits_.contains(p);
        if(exists&&state==base)--count;else if(!exists&&state!=base)++count;
        changes.push_back({p,block(p),state,base});
        drops+=simulation_->container_drops(p,state);
    }
    if(count>100000||(drops&&simulation_->mobs.size()+drops+1>4096))return false;
    for(const auto& c:changes)if(c.after==c.base)edits_.erase(c.p);else edits_[c.p]=c.after;
    for(const auto& c:changes)if(c.before!=c.after){dirty_=true;simulation_->changed(c.p,c.before,c.after);for(auto* player:players)player->block_changed(c.p,c.after);}
    for(const auto& c:changes)if(c.before!=c.after) {
        queue_bulb_light(c.p,c.before,c.after);
        simulation_->update_copper_bulbs(c.p,c.before,c.after);
    }
    return true;
}
void World::save() {
    for(auto& [id,world]:dimensions_)world->save();
    if(!dirty_ || directory_.empty()) return;
    const auto save_start=Performance::Clock::now();
    nlohmann::json data{{"format",2},{"protocol",774},{"generator","cuda-craft-"+options_.generator+"-v1"},
        {"seed",options_.seed},{"edits",nlohmann::json::array()}};
    for(const auto& [p,state]:edits_) data["edits"].push_back({p.x,p.y,p.z,state});
    data["simulation"]=nlohmann::json::parse(simulation_->save());
    const auto temporary=directory_/"blocks.json.tmp", destination=directory_/"blocks.json";
    { std::ofstream out(temporary,std::ios::binary|std::ios::trunc); out<<data.dump(); out.flush(); if(!out) throw std::runtime_error("Cannot write world edits"); }
#ifdef _WIN32
    if(!MoveFileExW(temporary.c_str(),destination.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)) throw std::runtime_error("Cannot replace world edit file");
#else
    std::filesystem::rename(temporary,destination);
#endif
    dirty_=false;
    ++performance.saves;performance.last_save_ms=Performance::elapsed(save_start);
}
std::string World::place_template(std::string name,BlockPos origin,int variant) {
    if(name.starts_with("minecraft:"))name.erase(0,10);
    if(name.empty()||name.size()>180||name.front()=='/'||name.back()=='/'||name.find("//")!=name.npos||name.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789_/")!=name.npos)
        throw std::runtime_error("Invalid template identifier");
    const auto path=std::filesystem::path("data")/"structures"/(name+".json");
    if(!std::filesystem::is_regular_file(path))throw std::runtime_error("Template not installed. Run tools/import-vanilla-structures.py; see data/structures/manifest.json for names.");
    if(std::filesystem::file_size(path)>8*1024*1024)throw std::runtime_error("Template exceeds size limit");
    std::ifstream input(path);const auto data=nlohmann::json::parse(input);
    if(data.at("format")!=1||data.at("protocol")!=774)throw std::runtime_error("Unsupported template version");
    const auto& palettes=data.at("palettes");
    if(variant<0||static_cast<std::size_t>(variant)>=palettes.size())throw std::runtime_error("Template palette variant out of range");
    const auto palette=palettes.at(variant).get<std::vector<int>>();
    for(int state:palette)if(!simdata::valid_state(state))throw std::runtime_error("Invalid template block state");
    const auto size=data.at("size").get<std::vector<int>>();
    if(size.size()!=3||std::any_of(size.begin(),size.end(),[](int n){return n<0||n>256;}))throw std::runtime_error("Invalid template dimensions");
    const auto& records=data.at("blocks");if(!records.is_array()||records.size()>100000)throw std::runtime_error("Template block limit exceeded");
    auto next=edits_;std::map<BlockPos,std::pair<int,int>> changed;std::size_t drops=0;
    for(const auto& row:records) {
        const auto v=row.get<std::vector<int>>();
        if(v.size()!=4||v[0]<0||v[0]>=size[0]||v[1]<0||v[1]>=size[1]||v[2]<0||v[2]>=size[2]||v[3]<0||static_cast<std::size_t>(v[3])>=palette.size())throw std::runtime_error("Invalid template block");
        BlockPos p{origin.x+v[0],origin.y+v[1],origin.z+v[2]};
        if(!valid(p)||p.y<=min_y()||p.y>=min_y()+height())throw std::runtime_error("Entire template must fit inside world bounds");
        const int state=palette[v[3]],before=block(p);
        drops+=simulation_->container_drops(p,state);
        if(!changed.emplace(p,std::pair(before,state)).second)throw std::runtime_error("Duplicate template position");
        if(state==base_block(p))next.erase(p);else next[p]=state;
    }
    if(next.size()>100000)throw std::runtime_error("World edit limit exceeded; no blocks were changed");
    if(drops&&simulation_->mobs.size()+drops+1>4096)throw std::runtime_error("Not enough entity capacity for container contents; no blocks were changed");
    edits_.swap(next);dirty_=true;
    std::set<std::pair<int,int>> refresh;
    for(const auto& [p,values]:changed)if(values.first!=values.second) {simulation_->changed(p,values.first,values.second);refresh.insert({terrain_math::floor_div(p.x,16),terrain_math::floor_div(p.z,16)});}
    for(const auto& [p,values]:changed)if(values.first!=values.second)simulation_->update_copper_bulbs(p,values.first,values.second);
    for(const auto& [cx,cz]:refresh)for(auto* player:players)player->refresh_chunk(cx,cz);
    return "Placed "+name+" ("+std::to_string(changed.size())+" blocks). Block-entity data skipped: "+std::to_string(data.value("unsupported_block_entities",0))+"; entities skipped: "+std::to_string(data.value("unsupported_entities",0))+". Template pieces are not assembled into full structures.";
}
void World::tick() {
    const auto now=std::chrono::steady_clock::now();
    if(paused)last_tick_=now;
    int catchup=0;
    while(now-last_tick_>=std::chrono::milliseconds(50)&&catchup++<5) {advance_ticks(1);last_tick_+=std::chrono::milliseconds(50);}
    if(now-last_tick_>std::chrono::milliseconds(250)){performance.dropped_tick_slots+=static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(now-last_tick_).count()/50);last_tick_=now;}
    if(now-last_save_>=std::chrono::seconds(1)) { save(); last_save_=now; }
}
void World::advance_ticks(int count) {for(int i=0;i<count;++i){auto start=Performance::Clock::now();simulation_->step();for(auto& [id,world]:dimensions_)world->advance_ticks(1);performance.tick(Performance::elapsed(start));}}
long World::tick_wait_microseconds() const {return static_cast<long>(std::clamp<std::int64_t>(std::chrono::duration_cast<std::chrono::microseconds>(last_tick_+std::chrono::milliseconds(50)-std::chrono::steady_clock::now()).count(),0,50000));}
bool World::solid(BlockPos p) const {int s=block(p);return simdata::block(s).solid&&!simdata::repeater(s);}
void World::activate_chunk(int cx,int cz) {
    auto blocks=options_.generator=="flat"?base_:terrain(cx,cz).blocks;
    for(const auto& [p,state]:edits_)if(terrain_math::floor_div(p.x,16)==cx&&terrain_math::floor_div(p.z,16)==cz)blocks[(p.y+64)*256+(p.z&15)*16+(p.x&15)]=static_cast<unsigned>(state);
    simulation_->activate(cx,cz,blocks);
}
void World::queue_bulb_light(BlockPos p,int before,int after) {
    if(players.empty()||simdata::copper_bulb_light(before)==simdata::copper_bulb_light(after))return;
    for(auto* player:players)player->refresh_light(terrain_math::floor_div(p.x,16),terrain_math::floor_div(p.z,16));
}
protocol::Bytes World::chunk(int cx,int cz,bool light_only) const {
    Packet result(light_only?0x2f:0x2c);
    if(light_only)result.var(cx).var(cz);
    else result.u32(static_cast<std::uint32_t>(cx)).u32(static_cast<std::uint32_t>(cz));
    auto blocks=options_.generator=="flat"?base_:terrain(cx,cz).blocks;
    for(const auto& [p,state]:edits_) if(terrain_math::floor_div(p.x,16)==cx && terrain_math::floor_div(p.z,16)==cz)
        blocks[static_cast<std::size_t>((p.y+64)*256+(p.z&15)*16+(p.x&15))]=static_cast<std::uint32_t>(state);
    if(light_only) {
        append_chunk_light(result,blocks);
        return result.data;
    }
    auto get=[&](int x,int y,int z) { return static_cast<int>(blocks[static_cast<std::size_t>((y+64)*256+z*16+x)]); };
    std::array<int,256> heights{},motion_heights{};
    for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
        int top=319;
        while(top> -64 && get(x,top,z)==0) --top;
        heights[static_cast<std::size_t>(z*16+x)]=std::clamp(top-min_y()+1,0,height());
        while(top> -64) {
            const int state=get(x,top,z);
            if(state!=terrain_ids::short_grass && state!=terrain_ids::dandelion && state!=terrain_ids::poppy) break;
            --top;
        }
        motion_heights[static_cast<std::size_t>(z*16+x)]=std::clamp(top-min_y()+1,0,height());
    }
    result.var(2);
    for(int type:{1,4}) {
        result.var(type).var(37);
        for(int group=0;group<37;++group) {
            std::uint64_t packed=0;
            for(int i=0;i<7;++i) if(group*7+i<256) packed|=static_cast<std::uint64_t>((type==1?heights:motion_heights)[static_cast<std::size_t>(group*7+i)])<<(i*9);
            result.u64(packed);
        }
    }
    Packet sections;
    for(int sy=(min_y()+64)/16;sy<(min_y()+64+height())/16;++sy) {
        std::array<unsigned,4096> indices{};
        std::vector<int> palette;
        int nonair=0;
        for(int i=0;i<4096;++i) {
            const int state=get(i&15,sy*16-64+(i>>8),(i>>4)&15);
            if(state) ++nonair;
            auto found=std::find(palette.begin(),palette.end(),state);
            if(found==palette.end()) { indices[static_cast<std::size_t>(i)]=static_cast<unsigned>(palette.size()); palette.push_back(state); }
            else indices[static_cast<std::size_t>(i)]=static_cast<unsigned>(found-palette.begin());
        }
        sections.u16(static_cast<std::uint16_t>(nonair));
        if(palette.size()==1) sections.u8(0).var(palette[0]);
        else {
            const bool direct=palette.size()>256;
            const unsigned bits=direct?std::bit_width(static_cast<unsigned>(simdata::blocks[std::size(simdata::blocks)-1].last)):static_cast<unsigned>(std::max(4,std::bit_width(static_cast<unsigned>(palette.size()-1))));
            sections.u8(static_cast<std::uint8_t>(bits));
            if(!direct){sections.var(static_cast<int>(palette.size()));for(int state:palette)sections.var(state);}
            const unsigned per_word=64/bits;
            for(unsigned i=0;i<4096;i+=per_word) {
                std::uint64_t packed=0;
                for(unsigned j=0;j<per_word && i+j<4096;++j) packed|=static_cast<std::uint64_t>(direct?palette[indices[i+j]]:indices[i+j])<<(j*bits);
                sections.u64(packed);
            }
        }
        std::vector<int> biome_palette;
        std::array<unsigned,64> biome_indices{};
        for(int i=0;i<64;++i) {
            const int id=biome(cx*16+(i&3)*4,sy*16-64+(i>>4)*4,cz*16+((i>>2)&3)*4);
            const auto found=std::find(biome_palette.begin(),biome_palette.end(),id);
            biome_indices[static_cast<std::size_t>(i)]=static_cast<unsigned>(found-biome_palette.begin());
            if(found==biome_palette.end()) biome_palette.push_back(id);
        }
        if(biome_palette.size()==1) sections.u8(0).var(biome_palette[0]);
        else {
            unsigned bits=std::bit_width(static_cast<unsigned>(biome_palette.size()-1));
            const bool direct=bits>3;
            if(direct) bits=7; // 65 registered biomes in 1.21.11.
            sections.u8(static_cast<std::uint8_t>(bits));
            if(!direct) { sections.var(static_cast<int>(biome_palette.size())); for(int id:biome_palette) sections.var(id); }
            const unsigned per_word=64/bits;
            for(unsigned i=0;i<64;i+=per_word) {
                std::uint64_t packed=0;
                for(unsigned j=0;j<per_word && i+j<64;++j) packed|=static_cast<std::uint64_t>(direct?biome_palette[biome_indices[i+j]]:biome_indices[i+j])<<(j*bits);
                sections.u64(packed);
            }
        }
    }
    result.var(static_cast<int>(sections.data.size())).raw(sections.data).var(0); // no block entities
    append_chunk_light(result,blocks);
    return result.data;
}
void World::append_chunk_light(Packet& result,const std::vector<std::uint32_t>& blocks) const {
    using namespace terrain_ids;
    auto opacity=[](int state) {
        if(simdata::water(state))return 1;
        if(simdata::lava(state)||!simdata::block(state).solid||simdata::repeater(state))return 0;
        if(state==air || state==short_grass || state==dandelion || state==poppy || state==562 || state==lava) return 0;
        if(state==water || state==ice || state==oak_leaves || state==birch_leaves || state==spruce_leaves || state==jungle_leaves ||
           state==acacia_leaves || state==dark_oak_leaves || state==mangrove_leaves || state==cherry_leaves || state==pale_oak_leaves) return 1;
        return 15;
    };
    std::vector<std::uint8_t> sky(blocks.size()),light(blocks.size()),damping(blocks.size());
    std::vector<int> queue; queue.reserve(blocks.size());
    for(int i=0;i<static_cast<int>(blocks.size());++i) damping[static_cast<std::size_t>(i)]=static_cast<std::uint8_t>(opacity(static_cast<int>(blocks[static_cast<std::size_t>(i)])));
    for(int column=0;column<256;++column) {
        int sunlight=dimension_?0:15;
        for(int y=383;y>=0;--y) {
            const auto i=static_cast<std::size_t>(y*256+column);
            sunlight=std::max(0,sunlight-damping[i]); sky[i]=static_cast<std::uint8_t>(sunlight);
            if(sunlight) queue.push_back(static_cast<int>(i));
        }
    }
    auto spread=[&](std::vector<std::uint8_t>& values) {
        for(std::size_t head=0;head<queue.size();++head) {
            const int i=queue[head],x=i&15,z=(i>>4)&15,y=i>>8;
            const int neighbors[6]={x>0?i-1:-1,x<15?i+1:-1,z>0?i-16:-1,z<15?i+16:-1,y>0?i-256:-1,y<383?i+256:-1};
            for(int next:neighbors) if(next>=0) {
                const int value=values[static_cast<std::size_t>(i)]-std::max(1,static_cast<int>(damping[static_cast<std::size_t>(next)]));
                if(value>values[static_cast<std::size_t>(next)]) { values[static_cast<std::size_t>(next)]=static_cast<std::uint8_t>(value); queue.push_back(next); }
            }
        }
        queue.clear();
    };
    spread(sky);
    for(int i=0;i<static_cast<int>(blocks.size());++i) {
        const int s=static_cast<int>(blocks[static_cast<std::size_t>(i)]);
        const int emitted=simdata::lava(s)||s==simdata::redstone_lamp_first?15:s>=3169&&s<=3173?14:simdata::torch(s)&&(s-simdata::redstone_torch_first)%2==0?7:simdata::copper_bulb_light(s);
        if(emitted){light[static_cast<std::size_t>(i)]=static_cast<std::uint8_t>(emitted);queue.push_back(i);}
    }
    spread(light);
    const int light_sections=height()/16+2;
    const std::uint64_t light_mask=(1ull<<light_sections)-1;
    result.var(1).u64(light_mask).var(1).u64(light_mask).var(0).var(0);
    for(const auto* values:{&sky,&light}) {
        result.var(light_sections);
        for(int section=0;section<light_sections;++section) {
            result.var(2048);
            for(int i=0;i<4096;i+=2) {
                if(section==0 || section==light_sections-1) result.u8(!dimension_&&section==light_sections-1 && values==&sky?255:0);
                else {
                    const auto index=static_cast<std::size_t>((section-1+(min_y()+64)/16)*4096+i);
                    result.u8(static_cast<std::uint8_t>((*values)[index]|((*values)[index+1]<<4)));
                }
            }
        }
    }
}
}
