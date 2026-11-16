#include "cudacraft/noise_terrain.hpp"
#include "cudacraft/biome_source.hpp"
#include "cudacraft/generated_density_data.hpp"
#include "cudacraft/generated_game_data.hpp"
#include "cudacraft/decorations.hpp"
extern "C" {
#include "core/src/hc_surface.h"
#include "core/src/hc_carvers.h"
}
#include <cstring>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <map>
#include <algorithm>

namespace cudacraft {
struct NoiseTerrain::Impl {
    const BiomeSource& biomes;
    std::vector<unsigned char> storage = std::vector<unsigned char>(32u << 20);
    hc_arena_t arena{};
    hc_df_graph_t graph{};
    hc_noise_roots_t roots{};
    hc_surface_t surface{};
    std::array<hc_carver_t,3> carvers{};
    hc_biome_reg_t registry{};
    std::vector<int> states;
    std::array<std::uint16_t,65> biome_ids{};
    std::size_t checkpoint = 0;
    struct Cached { TerrainChunk chunk; std::uint64_t used; };
    std::map<std::pair<int,int>,Cached> cache;
    std::uint64_t cache_clock=0;

    explicit Impl(const BiomeSource& source) : biomes(source) {
        hc_arena_init(&arena, storage.data(), storage.size());
        hc_biome_reg_init(&registry, &arena);
        std::vector<hc_df_source_t> dfs, noises, tags;
        std::unordered_map<std::string,const hc_json_t*> carver_json;
        const hc_json_t* settings = nullptr;
        for(const auto& e : density_data::entries) {
            const char* error = nullptr;
            const auto* value = hc_json_parse(e.json, &arena, &error, nullptr);
            if(!value) throw std::runtime_error(std::string("Worldgen data: ") + (error ? error : "arena exhausted"));
            if(std::strcmp(e.kind,"density_function")==0) dfs.push_back({e.name,value});
            else if(std::strcmp(e.kind,"noise")==0) noises.push_back({e.name,value});
            else if(std::strcmp(e.kind,"block_tag")==0) tags.push_back({e.name,value});
            else if(std::strcmp(e.kind,"configured_carver")==0) carver_json.emplace(e.name,value);
            else if(std::strcmp(e.kind,"noise_settings")==0) settings=value;
            else if(std::strcmp(e.kind,"biome")==0) {
                const int id=hc_biome_intern(&registry,e.name,static_cast<int>(std::strlen(e.name)));
                if(id<0) throw std::runtime_error("Worldgen biome registry exhausted");
                const auto* temp=hc_json_get(value,"temperature");
                hc_biome_set_climate(&registry,id,static_cast<float>(temp->num),
                    static_cast<std::uint8_t>(hc_json_streq(hc_json_get(value,"temperature_modifier"),"frozen")?HC_BIOME_TEMP_MOD_FROZEN:HC_BIOME_TEMP_MOD_NONE));
            }
        }
        if(!settings) throw std::runtime_error("Missing overworld settings");
        hc_df_compiler_t compiler{};
        if(hc_df_compiler_init(&compiler,&graph,&arena,biomes.seed(),dfs.data(),static_cast<int>(dfs.size()),noises.data(),static_cast<int>(noises.size()))<0)
            throw std::runtime_error("Density graph allocation failed");
        const auto* router=hc_json_get(settings,"noise_router");
        auto compile=[&](const char* name) {
            const auto* expr=hc_json_get(router,name);
            if(!expr) throw std::runtime_error(std::string("Missing noise router slot: ")+name);
            int id=hc_df_compile_expr(&compiler,expr);
            if(id<0) throw std::runtime_error(std::string("Density compiler: ")+compiler.err);
            return id;
        };
        roots.final_density=compile("final_density");
        roots.barrier=compile("barrier");
        roots.fluid_level_floodedness=compile("fluid_level_floodedness");
        roots.fluid_level_spread=compile("fluid_level_spread");
        roots.lava=compile("lava"); roots.erosion=compile("erosion"); roots.depth=compile("depth");
        roots.preliminary_surface_level=compile("preliminary_surface_level");
        roots.vein_toggle=compile("vein_toggle"); roots.vein_ridged=compile("vein_ridged"); roots.vein_gap=compile("vein_gap");
        if(hc_surface_init(&surface,&arena,biomes.seed(),settings,noises.data(),static_cast<int>(noises.size()),&registry)<0)
            throw std::runtime_error(std::string("Surface compiler: ")+surface.err);
        hc_biome_temp_prewarm();
        const char* carver_names[]={"minecraft:cave","minecraft:cave_extra_underground","minecraft:canyon"};
        for(int i=0;i<3;++i) {
            const char* error=nullptr;
            if(hc_carver_init(&carvers[i],carver_json.at(carver_names[i]),tags.data(),static_cast<int>(tags.size()),&error)<0)
                throw std::runtime_error(std::string("Carver compiler: ")+error);
        }
        std::unordered_map<std::string,int> mapping;
        for(const auto& s:density_data::states) mapping.emplace(s.name,static_cast<int>(s.id));
        states.resize(HC_B_COUNT,-1);
        for(int i=0;i<HC_B_COUNT;++i) {
            const char* name=hc_block_name(static_cast<std::uint16_t>(i));
            if(name) { auto it=mapping.find(name); if(it!=mapping.end()) states[i]=it->second; }
        }
        // The wire registry is in generated login-packet order, not Cubiomes order.
        // Emitted separately by our version-pinned registry generator.
        for(std::size_t i=0;i<65;++i) {
            const auto* name=density_data::biome_names[i];
            const int id=hc_biome_find(&registry,name,static_cast<int>(std::strlen(name)));
            if(id<0) throw std::runtime_error("Missing 1.21.11 surface biome");
            biome_ids[i]=static_cast<std::uint16_t>(id);
        }
        checkpoint=arena.off;
    }
    TerrainChunk chunk(int cx,int cz,bool carve) {
        arena.off=checkpoint;
        hc_chunk_t chunk{};
        hc_noise_chunk_t nc{};
        if(hc_chunk_init(&chunk,&arena,cx,cz)<0 || hc_nc_init(&nc,&arena,&graph,&roots,biomes.seed(),cx,cz,63)<0)
            throw std::runtime_error("Noise chunk arena exhausted");
        hc_gen_noise_stage(&chunk,&nc);
        // Surface lookup uses vanilla's hashed Voronoi biome zoom, including halo.
        std::vector<std::uint16_t> halo(96*12*12);
        TerrainChunk result;
        for(int dz=-1;dz<=1;++dz) for(int dx=-1;dx<=1;++dx) {
            const auto cells=biomes.chunk(cx+dx,cz+dz);
            if(dx==0 && dz==0) result.biomes=cells;
            for(int y=0;y<96;++y) for(int z=0;z<4;++z) for(int x=0;x<4;++x)
                halo[(y*12+(dz+1)*4+z)*12+(dx+1)*4+x]=biome_ids[cells[y*16+z*4+x]];
        }
        hc_biome_view_t view{(cx-1)*4,(cz-1)*4,12,-16,96,halo.data(),static_cast<std::int64_t>(biomes.seed_hash())};
        hc_gen_surface_stage(&chunk,&nc,&surface,&view);
        std::array<std::uint64_t,HC_CARVING_MASK_WORDS> mask{};
        if(carve) hc_gen_carvers_stage(&chunk,&nc,&surface,&view,biomes.seed(),carvers.data(),3,mask.data());
        result.blocks.resize(HC_BLOCKS);
        for(int i=0;i<HC_BLOCKS;++i) {
            const int state=states.at(chunk.states[i]);
            if(state<0) throw std::runtime_error(std::string("Unmapped 1.21.11 terrain state: ")+hc_block_name(chunk.states[i]));
            result.blocks[i]=static_cast<std::uint32_t>(state);
        }
        return result;
    }
    const TerrainChunk& cached(int cx,int cz) {
        const auto key=std::pair(cx,cz);
        auto it=cache.find(key);
        if(it!=cache.end()) { it->second.used=++cache_clock; return it->second.chunk; }
        if(cache.size()>=64) {
            const auto oldest=std::min_element(cache.begin(),cache.end(),[](const auto& a,const auto& b){return a.second.used<b.second.used;});
            cache.erase(oldest);
        }
        return cache.emplace(key,Cached{chunk(cx,cz,true),++cache_clock}).first->second.chunk;
    }
    terrain_math::Column column(int x,int z) {
        const auto& base=cached(terrain_math::floor_div(x,16),terrain_math::floor_div(z,16));
        int y=319;
        auto at=[&](int height){return static_cast<int>(base.blocks[(height+64)*256+(z&15)*16+(x&15)]);};
        while(y> -64 && (at(y)==terrain_ids::air || at(y)==terrain_ids::water || at(y)==terrain_ids::lava)) --y;
        const int biome=base.biomes[((y+64)/4)*16+((z&15)/4)*4+(x&15)/4];
        return {y,biome,at(y),y> -64?at(y-1):at(y)};
    }
};
NoiseTerrain::NoiseTerrain(const BiomeSource& biomes):impl_(std::make_unique<Impl>(biomes)){}
NoiseTerrain::~NoiseTerrain()=default;
TerrainChunk NoiseTerrain::chunk(int cx,int cz,bool carve) const { return impl_->chunk(cx,cz,carve); }
terrain_math::Column NoiseTerrain::column(int x,int z) const { return impl_->column(x,z); }
TerrainChunk NoiseTerrain::decorated_chunk(int cx,int cz) const {
    auto result=impl_->cached(cx,cz);
    std::array<terrain_math::Column,256> columns;
    for(int z=0;z<16;++z) for(int x=0;x<16;++x) columns[z*16+x]=column(cx*16+x,cz*16+z);
    decorate_terrain(result,impl_->biomes.seed(),cx,cz,columns,[&](int x,int z){return column(x,z);});
    return result;
}
}
