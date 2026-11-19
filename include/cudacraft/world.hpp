#pragma once
#include "cudacraft/packet.hpp"
#include "cudacraft/terrain.hpp"
#include "cudacraft/performance.hpp"
#include <chrono>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>

namespace cudacraft {
class GameSession;
class NoiseTerrain;
class Simulation;
class Administration;
class WorldLock;
struct PortalFrame { BlockPos origin; int width,height; bool along_x; };
class World {
public:
    explicit World(std::filesystem::path directory = {}, TerrainOptions options = {}, World* parent=nullptr, int dimension=0);
    ~World();
    int block(BlockPos p) const;
    bool set(BlockPos p, int state);
    bool set_blocks(const std::vector<std::pair<BlockPos,int>>& blocks);
    std::string place_template(std::string name, BlockPos origin, int variant=0);
    protocol::Bytes chunk(int x, int z, bool light_only=false) const;
    void save();
    void tick();
    long tick_wait_microseconds() const;
    void advance_ticks(int count);
    Administration& admin(){return root().admin_?*root().admin_:*admin_;}
    World& root(){return parent_?*parent_:*this;}
    World& dimension(int id);
    std::optional<PortalFrame> portal_frame(BlockPos point) const;
    std::optional<PortalFrame> find_or_create_portal(BlockPos target,bool along_x);
    int dimension_id() const{return dimension_;}
    int dimension_type() const{return dimension_==1?3:dimension_==2?2:0;}
    const char* dimension_name() const{return dimension_==1?"minecraft:the_nether":dimension_==2?"minecraft:the_end":"minecraft:overworld";}
    int min_y() const{return dimension_?0:-64;}
    int height() const{return dimension_?256:384;}
    std::vector<GameSession*> all_players();
    mutable Performance performance;
    bool local_session=false,paused=false;
    std::size_t cached_chunks() const{return terrain_cache_.size();}
    std::size_t edit_count() const{return edits_.size();}
    Simulation& simulation() { return *simulation_; }
    const Simulation& simulation() const { return *simulation_; }
    bool solid(BlockPos p) const;
    void mark_dirty() { dirty_=true; }
    void activate_chunk(int x,int z);
    std::vector<GameSession*> players;
    int next_entity = 1;
    static bool valid(BlockPos p);
    static int item_state(int item);
    static int state_item(int state);
    const TerrainOptions& options() const { return options_; }
    BlockPos spawn() const;
    int biome(int x, int y, int z) const;
    std::uint64_t seed_hash() const;
private:
    void queue_bulb_light(BlockPos p,int before,int after);
    void append_chunk_light(Packet& packet,const std::vector<std::uint32_t>& blocks) const;
    // Destroy last, after dimension saves and all other world-owned state.
    std::unique_ptr<WorldLock> lock_;
    World* parent_=nullptr;
    int dimension_=0;
    std::map<int,std::unique_ptr<World>> dimensions_;
    std::filesystem::path directory_;
    std::map<BlockPos,int> edits_;
    std::vector<std::uint32_t> base_;
    TerrainOptions options_;
    std::unique_ptr<BiomeSource> biome_source_;
    std::unique_ptr<NoiseTerrain> noise_terrain_;
    std::unique_ptr<Simulation> simulation_;
    std::unique_ptr<Administration> admin_;
    struct CachedChunk { TerrainChunk terrain; std::uint64_t used; };
    mutable std::map<std::pair<int,int>,CachedChunk> terrain_cache_;
    mutable std::uint64_t cache_clock_ = 0;
    const TerrainChunk& terrain(int cx, int cz) const;
    int base_block(BlockPos p) const;
    bool dirty_ = false;
    std::chrono::steady_clock::time_point last_save_ = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_tick_ = std::chrono::steady_clock::now();
};
}
