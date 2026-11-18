#pragma once
#include "cudacraft/auth.hpp"
#include "cudacraft/world.hpp"
#include "cudacraft/simulation.hpp"
#include <functional>
#include <set>
#include <deque>

namespace cudacraft {
class GameSession {
public:
    using Send = std::function<void(std::span<const std::uint8_t>)>;
    GameSession(World& world, auth::Profile profile, Send send);
    ~GameSession();
    void start();
    void handle(std::span<const std::uint8_t> payload);
    void poll();
    void block_changed(BlockPos p, int state);
    void refresh_chunk(int cx,int cz) {if(sees(cx,cz))refresh_.insert({cx,cz});}
    void container_changed(BlockPos position,bool removed=false);
    void container_properties();
    void container_progress(BlockPos position);
    void tell(std::string_view message);
    bool expired() const { return expired_; }
    bool survival() const { return mode_==0; }
    bool alive() const { return health_>0; }
    bool sees(int cx,int cz) const { return loaded_.contains({cx,cz}); }
    void simulation_tick();
    void damage(float amount,std::string_view cause,bool force=false);
    int add_item(ItemStack stack);
    float attack_damage();
    void mob_hurt(int id);
    void collect(int mob,int player,int count);
    World& world(){return *world_;}
    void change_dimension(int dimension,bool through_portal=false);
    void command_tree();
    void kick(std::string_view reason);
    bool closing() const{return kicked_;}
    int entity_id;
    auth::Profile profile;
    double x=0.5,y=64,z=0.5;
    float yaw=0,pitch=0;
private:
    friend class Administration;
    World* world_;
    Send send_;
    std::array<ItemStack,46> inventory_{};
    ItemStack cursor_{};
    std::array<ItemStack,9> crafting_{};
    bool crafting_open_=false;
    std::vector<BlockPos> container_positions_;
    BlockPos container_anchor_{};
    int container_type_=0,container_window_=2,next_container_window_=2;
    bool container_valid() const;
    bool open_container(BlockPos position);
    void container_click(int index,int button,int mode);
    int current_window() const {return !container_positions_.empty()?container_window_:crafting_open_?1:0;}
    ItemStack craft_result_{};
    std::vector<int> craft_slots_;
    int mode_=1,food_=20,air_=300;
    float health_=20,saturation_=5,exhaustion_=0;
    double fall_start_=64;
    bool falling_=false;
    int portal_ticks_=0;
    std::uint64_t portal_cooldown_=0;
    std::uint64_t hurt_until_=0,attack_after_=0,eat_at_=0;
    int eating_slot_=-1;
    ItemStack eating_item_{};
    BlockPos mining_{};
    int mining_state_=0;
    std::uint64_t mining_start_=0;
    bool mining_active_=false;
    std::set<int> seen_mobs_;
    std::uint64_t last_mob_sync_=~std::uint64_t{0};
    std::set<std::pair<int,int>> loaded_;
    std::deque<std::pair<int,int>> pending_;
    std::set<std::pair<int,int>> refresh_;
    int center_x_=100000, center_z_=100000, selected_=0, inventory_revision_=0;
    int teleport_=1;
    bool awaiting_teleport_=true, joined_=false, expired_=false;
    bool kicked_=false,monitor_=false;
    std::uint64_t keepalive_=0;
    std::chrono::steady_clock::time_point last_keepalive_=std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point last_chunks_=std::chrono::steady_clock::now();
    void send(const Packet& p) { try {send_(p.data);} catch(const protocol::Error&) {expired_=true;} }
    void update_view();
    void teleport_home();
    void set_slot(int slot,int item,int count=64,int damage=0);
    void inventory_sync();
    void health_sync();
    void set_mode(int mode);
    void save_player();
    void respawn();
    void sync_mobs();
    void inventory_click(protocol::Reader& reader);
    void update_recipe();
    void close_crafting();
    void use_item(int hand);
    bool use_bucket(BlockPos clicked,int face,int slot);
    bool use_portal_item(BlockPos clicked,int face,int slot);
    void finish_mining(BlockPos p);
    void wear_tool();
    void player_info(const GameSession& player);
    void spawn(const GameSession& player);
    bool reachable(BlockPos p) const;
};
}
