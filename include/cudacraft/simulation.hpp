#pragma once
#include "cudacraft/packet.hpp"
#include <array>
#include <map>
#include <set>
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
};
class Simulation {
public:
    explicit Simulation(World& world):world_(world){}
    void changed(BlockPos p,int before,int after,bool notify_observers=true);
    void update_copper_bulbs(BlockPos changed,int before,int after);
    void update_droppers(BlockPos changed,int before,int after);
    void step();
    void activate(int cx,int cz,const std::vector<std::uint32_t>& blocks);
    bool interact(BlockPos p);
    bool fertilize(BlockPos p);
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
    int spawn(int type,double x,double y,double z,ItemStack drop={});
    void attack(GameSession& player,int entity);
    std::string save() const;
    void load(std::string_view json);
    std::uint64_t tick=0;
    std::uint64_t day_time=6000;
    std::map<std::string,PlayerState> saved_players;
    std::map<int,Mob> mobs;
    std::map<BlockPos,Container> containers;
    std::size_t pending_water() const { return water_.size(); }
private:
    World& world_;
    std::map<BlockPos,std::uint64_t> water_;
    std::set<BlockPos> circuits_;
    std::set<BlockPos> copper_bulbs_;
    std::set<BlockPos> piston_checks_;
    std::set<BlockPos> hoppers_;
    std::set<BlockPos> droppers_;
    std::map<BlockPos,int> comparator_outputs_;
    std::set<BlockPos> shape_updates_;
    std::set<BlockPos> portal_checks_;
    std::set<BlockPos> ticking_blocks_;
    BlockPos block_cursor_{};
    std::map<BlockPos,std::uint64_t> button_releases_;
    struct Event { std::uint64_t due; int state; };
    std::map<BlockPos,Event> devices_;
    bool redstone_dirty_=false;
    void schedule_water(BlockPos p,std::uint64_t delay=5);
    void flow(BlockPos p);
    void redstone();
    int emitted_power(BlockPos from,BlockPos to,bool wires) const;
    int neighbor_power(BlockPos position,bool wires) const;
    int directional_input(BlockPos position,BlockPos direction) const;
    int comparator_output(BlockPos position,int state) const;
    std::optional<int> container_signal(BlockPos position) const;
    void schedule_directional(BlockPos position);
    void tick_pistons();
    void update_shapes();
    void tick_furnaces();
    void tick_hoppers();
    void dispense_dropper(BlockPos position,int state);
    void tick_blocks();
    void validate_portals();
    void entities();
};
}
