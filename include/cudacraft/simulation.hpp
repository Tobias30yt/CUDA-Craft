#pragma once
#include "cudacraft/packet.hpp"
#include <array>
#include <deque>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace cudacraft {
class World;
class GameSession;
struct ItemStack { int item=0,count=0,damage=0; auto operator<=>(const ItemStack&) const=default; };
struct Container {
    int type=0;
    std::array<ItemStack,27> items{};
    int burn_left=0,burn_total=0,cook_progress=0,cook_total=200,cook_input=0;
    int transfer_cooldown=0;
    std::uint64_t last_transfer_tick=0;
};
struct PlayerState {
    std::array<ItemStack,46> inventory{};
    std::array<ItemStack,9> crafting{};
    ItemStack cursor{};
    double x=0,y=64,z=0;
    float health=20,saturation=5,exhaustion=0;
    int food=20,mode=1;
    int dimension=0;
};
struct Mob {
    int id=0,type=100;
    double x=0,y=64,z=0,vx=0,vy=0,vz=0;
    float health=10,yaw=0;
    std::uint64_t born=0,next_attack=0,knockback_until=0,angry_until=0;
    ItemStack drop{};
    int item_ticks=0,item_age=0;
    bool item_on_ground=false,item_stuck_snow=false;
    int portal_cooldown=0;
    std::optional<BlockPos> portal_entry;
    std::uint64_t last_world_tick=0;
};
struct Vehicle {
    int type=0;
    double x=0,y=0,z=0,vx=0,vy=0,vz=0;
    float yaw=0,pitch=0;
    int portal_cooldown=0;
    bool enabled=true;
    std::array<ItemStack,27> items{};
    std::optional<BlockPos> portal_entry;
    std::uint64_t last_world_tick=0;
};
struct PrimedTnt {
    double x=0,y=0,z=0,vx=0,vy=0,vz=0;
    int fuse=80;
    bool stuck_snow=false;
};
class Simulation {
public:
    explicit Simulation(World& world):world_(world){}
    void changed(BlockPos p,int before,int after,bool notify_observers=true);
    void update_power_receivers(BlockPos changed,int before,int after);
    void update_ejectors(BlockPos changed,int before,int after);
    void update_tnt(BlockPos changed);
    void update_rail_power(BlockPos changed,int before,int after);
    bool ignite_tnt(BlockPos position,int fuse=80);
    int spawn_tnt(PrimedTnt entity);
    void step();
    void activate(int cx,int cz,const std::vector<std::uint32_t>& blocks);
    bool interact(BlockPos p);
    bool fertilize(BlockPos p);
    bool pistons_moving() const { return !moving_pistons_.empty(); }
    std::optional<std::pair<int,std::array<double,3>>> piston_collision(BlockPos position) const;
    static bool ticking_block(int state);
    int placement(int item,int face,float yaw,float pitch=0) const;
    bool place_block(BlockPos position,int state);
    bool break_block(BlockPos position);
    static int container_type(int state);
    static int furnace_kind(int state);
    static int container_slots(int state);
    std::vector<BlockPos> container_positions(BlockPos position) const;
    Container* container(BlockPos position,bool create=false);
    std::size_t container_drops(BlockPos position,int replacement) const;
    void container_changed(BlockPos position,int before,int after);
    void inventory_signal_changed(BlockPos position);
    bool set_jukebox_record(BlockPos position,ItemStack record);
    bool use_jukebox(BlockPos position,ItemStack held);
    int spawn(int type,double x,double y,double z,ItemStack drop={});
    static int vehicle_slots(int type);
    int spawn_vehicle(Vehicle vehicle);
    void attack(GameSession& player,int entity);
    std::string save() const;
    void load(std::string_view json);
    std::uint64_t tick=0;
    std::uint64_t day_time=6000;
    std::map<std::string,PlayerState> saved_players;
    std::map<int,Mob> mobs;
    std::map<int,Vehicle> vehicles;
    std::map<int,PrimedTnt> primed_tnt;
    std::map<BlockPos,Container> containers;
    std::size_t pending_water() const { return water_.size(); }
private:
    friend class World;
    static constexpr BlockPos neighbor_update_order[]={{-1,0,0},{1,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1}};
    int note_block_state(BlockPos position,int state) const;
    World& world_;
    std::map<BlockPos,std::uint64_t> water_;
    std::set<BlockPos> circuits_;
    std::set<BlockPos> power_receivers_;
    std::set<BlockPos> piston_checks_;
    struct PistonMotion { int state; std::uint64_t completes; BlockPos direction; };
    std::map<BlockPos,PistonMotion> moving_pistons_;
    std::set<BlockPos> hoppers_;
    std::set<BlockPos> ejectors_;
    std::set<BlockPos> powered_rails_;
    std::set<BlockPos> detector_rails_;
    std::set<BlockPos> daylight_detectors_;
    std::set<BlockPos> tnt_blocks_;
    std::map<BlockPos,int> comparator_outputs_;
    std::deque<std::pair<BlockPos,std::uint64_t>> torch_switch_offs_;
    std::map<BlockPos,int> torch_off_counts_;
    std::map<BlockPos,std::uint64_t> playing_records_;
    std::set<BlockPos> shape_updates_;
    std::set<BlockPos> portal_checks_;
    std::set<BlockPos> ticking_blocks_;
    BlockPos block_cursor_{};
    std::map<BlockPos,std::uint64_t> button_releases_;
    struct Event { std::uint64_t due; int state,priority; std::uint64_t sequence; };
    std::map<BlockPos,Event> devices_;
    std::uint64_t next_device_sequence_=0;
    bool redstone_dirty_=false;
    std::set<BlockPos> redstone_updates_;
    void schedule_water(BlockPos p,std::uint64_t delay=5);
    void flow(BlockPos p);
    void redstone();
    int emitted_power(BlockPos from,BlockPos to,bool wires) const;
    int neighbor_power(BlockPos position,bool wires) const;
    int directional_input(BlockPos position,BlockPos direction,bool wires=true) const;
    bool repeater_locked(BlockPos position,BlockPos direction) const;
    int comparator_output(BlockPos position,int state) const;
    bool torch_input(BlockPos position,int state) const;
    std::optional<int> container_signal(BlockPos position) const;
    void schedule_directional(BlockPos position);
    void notify_analog_change(BlockPos position);
    void notify_diode_output(BlockPos position,int state);
    Event device_event(unsigned delay,int state,int priority=0);
    void tick_pistons();
    void finish_piston_motion();
    void schedule_moved_observer(BlockPos position,int moved_state);
    void update_powered_rails(const std::set<BlockPos>& affected);
    void update_shapes();
    void tick_furnaces();
    void tick_jukeboxes();
    void jukebox_signal_changed(BlockPos position);
    static int record_signal(int item);
    void tick_hoppers();
    void tick_vehicles();
    bool transfer_vehicle(int id,Vehicle& vehicle,BlockPos entrance);
    void collect_vehicle_items(Vehicle& vehicle);
    void tick_items();
    static int store_item(std::span<ItemStack> slots,ItemStack stack);
    void tick_tnt();
    void explode_tnt(int id,const PrimedTnt& source);
    const Vehicle* detector_vehicle(BlockPos position) const;
    int detector_inventory_signal(BlockPos position) const;
    void update_detector_rail(BlockPos position,bool scheduled);
    void dispense_container(BlockPos position,int state);
    void tick_blocks();
    void update_daylight(BlockPos position);
    void validate_portals();
    void entities();
};
}
