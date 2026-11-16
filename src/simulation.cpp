#include "cudacraft/simulation.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/world.hpp"
#include "cudacraft/game.hpp"
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cmath>
#include <deque>
#include <limits>

namespace cudacraft {
namespace {
constexpr BlockPos dirs[]={{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
constexpr BlockPos facing[]={{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
BlockPos add(BlockPos a,BlockPos b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
BlockPos opposite(BlockPos p){return {-p.x,-p.y,-p.z};}
BlockPos state_direction(int s){auto f=simdata::property_value(s,"facing");return f=="north"?facing[0]:f=="south"?facing[1]:f=="west"?facing[2]:facing[3];}
bool paired_vertical(int s){const auto half=simdata::property_value(s,"half");return half=="lower"||half=="upper";}
bool bed(int s){return std::string_view(simdata::block(s).name).ends_with("_bed");}
bool connects(int s){auto n=std::string_view(simdata::block(s).name);return n.ends_with("_fence")||n.ends_with("_pane")||n=="iron_bars";}
int fluid_state(int s){
    static const auto table=[] {std::array<int,simdata::blocks[std::size(simdata::blocks)-1].last+1> values{};for(int i=0;i<static_cast<int>(values.size());++i)values[i]=simdata::property_value(i,"waterlogged")=="true"?simdata::water_state:i;return values;}();
    return s>=0&&s<static_cast<int>(table.size())?table[s]:s;
}
bool full_cube(int s){auto n=std::string_view(simdata::block(s).name);return simdata::block(s).solid&&!connects(s)&&!n.ends_with("_wall")&&!n.ends_with("_door")&&!n.ends_with("_trapdoor")&&!n.ends_with("_fence_gate")&&!n.ends_with("_stairs")&&!bed(s)&&(!n.ends_with("_slab")||simdata::property_value(s,"type")=="double");}
bool inside(double x,double y,double z){return std::isfinite(x)&&std::isfinite(y)&&std::isfinite(z)&&std::abs(x)<1024&&std::abs(z)<1024&&y>=-64&&y<400;}
ItemStack read_stack(const nlohmann::json& v) {
    if(!v.is_array()||v.size()!=3) throw std::runtime_error("Invalid saved item");
    ItemStack s{v[0].get<int>(),v[1].get<int>(),v[2].get<int>()};
    if(s.item==0) { if(s.count||s.damage) throw std::runtime_error("Invalid empty item"); return s; }
    const auto* d=simdata::item(s.item);
    if(!d||s.count<1||s.count>d->stack||s.damage<0||s.damage>d->durability) throw std::runtime_error("Invalid saved item range");
    return s;
}
nlohmann::json stack_json(ItemStack s){return nlohmann::json::array({s.item,s.count,s.damage});}
}
void Simulation::schedule_water(BlockPos p,std::uint64_t delay) {
    if(!World::valid(p)||p.y==-64||water_.size()>=65536) return;
    const auto due=tick+delay;
    auto [i,inserted]=water_.emplace(p,due);
    if(!inserted) i->second=std::min(i->second,due);
    world_.mark_dirty();
}
void Simulation::changed(BlockPos p,int before,int after) {
    container_changed(p,before,after);
    devices_.erase(p);
    if(simdata::circuit(after)) circuits_.insert(p); else circuits_.erase(p);
    if(simdata::circuit(before)||simdata::circuit(after)||!circuits_.empty()) redstone_dirty_=true;
    if(simdata::water(fluid_state(after))) schedule_water(p);
    for(auto d:dirs) { auto n=add(p,d); if(World::valid(n)&&simdata::water(fluid_state(world_.block(n)))) schedule_water(n); }
    if(connects(after))shape_updates_.insert(p);
    for(auto d:dirs) {auto n=add(p,d);if(World::valid(n)&&connects(world_.block(n)))shape_updates_.insert(n);}
}
void Simulation::activate(int cx,int cz,const std::vector<std::uint32_t>& blocks) {
    // Activate exposed water, not every cell of an ocean. Edits also wake neighbors.
    for(int y=-63;y<320;++y) for(int z=0;z<16;++z) for(int x=0;x<16;++x) {
        const auto index=(y+64)*256+z*16+x;const int s=fluid_state(static_cast<int>(blocks[index]));
        if(!simdata::water(s)) continue;
        bool exposed=s!=86;
        for(auto d:dirs) {
            if(d.y==1) continue;
            const int xx=x+d.x,zz=z+d.z,yy=y+d.y;
            if(xx<0||xx>15||zz<0||zz>15||yy< -64) continue;
            const int n=fluid_state(static_cast<int>(blocks[(yy+64)*256+zz*16+xx]));
            exposed|=simdata::replaceable(n)&&!simdata::water(n);
        }
        if(exposed) schedule_water({cx*16+x,y,cz*16+z});
    }
}
void Simulation::flow(BlockPos p) {
    using namespace simdata;
    int state=fluid_state(world_.block(p));
    if(!water(state)) return;
    auto below=add(p,dirs[0]);
    int level=state-86;
    if(level!=0) {
        int best=8,sources=0;
        for(int d=2;d<6;++d) {
            int s=fluid_state(world_.block(add(p,dirs[d])));
            if(!water(s)) continue;
            if(s==86) ++sources;
            best=std::min(best,s>=94?1:s-86+1);
        }
        const int under=fluid_state(world_.block(below));
        if(sources>=2&&(world_.solid(below)||under==86)) level=0;
        else if(water(fluid_state(world_.block(add(p,dirs[1]))))) level=8;
        else level=best;
        if(level==8&&!water(fluid_state(world_.block(add(p,dirs[1]))))) { world_.set(p,0); return; }
        if(state!=86+level) world_.set(p,86+level);
    }
    // Contact cooling is supported; lava spread itself remains a separate system.
    for(auto d:dirs) {
        const auto n=add(p,d);const int s=world_.block(n);
        if(lava(s)) world_.set(n,s==102?obsidian_state:cobblestone_state);
    }
    auto can_fill=[&](BlockPos q){return World::valid(q)&&q.y> -64&&replaceable(world_.block(q));};
    const int under=fluid_state(world_.block(below));
    bool falls=can_fill(below)&&under!=86;
    if(falls && under!=94) world_.set(below,94);
    if(level!=0 && (falls||water(under))) return;
    const int spread=level>=8?1:level+1;
    if(spread>7) return;
    // Seek the nearest drop up to four horizontal steps, with deterministic order.
    auto slope=[&](BlockPos first,int forbidden) {
        std::deque<std::pair<BlockPos,int>> todo{{first,0}};std::set<BlockPos> seen{p,first};
        while(!todo.empty()) {
            const auto [q,dist]=todo.front();todo.pop_front();
            const auto down=add(q,dirs[0]);
            if(can_fill(down)&&world_.block(down)!=86) return dist;
            if(dist>=4) continue;
            for(int d=2;d<6;++d) {
                (void)forbidden; const auto n=add(q,dirs[d]);
                if(!seen.insert(n).second||!can_fill(n)||world_.block(n)==86) continue;
                todo.emplace_back(n,dist+1);
            }
        }
        return 1000;
    };
    std::array<int,4> distances{2000,2000,2000,2000};int best=2000;
    for(int d=2;d<6;++d) {
        const auto n=add(p,dirs[d]);const int s=world_.block(n);
        if(!can_fill(n)||s==86||(water(s)&&s-86<=spread)) continue;
        distances[d-2]=slope(n,d);best=std::min(best,distances[d-2]);
    }
    for(int d=2;d<6;++d) if(distances[d-2]==best&&best<2000) world_.set(add(p,dirs[d]),86+spread);
}
int Simulation::placement(int item,int face,float yaw) const {
    using namespace simdata;
    int look=(static_cast<int>(std::floor(yaw/90.0f+0.5f))%4+4)%4;
    const int toward[]={1,2,0,3};const int away[]={0,3,1,2};
    if(item==redstone_item) return redstone_wire_state;
    if(item==322)return face==1?3169:face>=2?3170+face-2:0;
    if(item==lever_item) {
        int side=face==1?0:face==0?2:1;
        int dir=side==1?std::clamp(face-2,0,3):away[look];
        return lever_first+side*8+dir*2+1;
    }
    if(item==redstone_torch_item) return face==1?redstone_torch_first:face>=2?redstone_wall_torch_first+(face-2)*2:0;
    if(item==repeater_item) return repeater_first+away[look]*4+3;
    (void)toward;
    if(item==redstone_lamp_item)return redstone_lamp_state;
    if(item==redstone_block_item)return redstone_block_state;
    if(item==crafting_table_item)return crafting_table_state;
    int state=World::item_state(item);
    state=with_property(state,"axis",face<2?"y":face<4?"z":"x");
    constexpr const char* directions[]={"south","west","north","east"};
    const std::string_view name=block(state).name;
    const bool along=name.ends_with("_stairs")||name.ends_with("_door")||name.ends_with("_bed")||name.ends_with("_fence_gate");
    state=with_property(state,"facing",directions[(look+(along?0:2))%4]);
    state=with_property(state,"rotation",std::to_string((static_cast<int>(std::floor((yaw+180)*16/360+.5))%16+16)%16));
    state=with_property(state,"waterlogged","false");
    return state;
}
bool Simulation::interact(BlockPos p) {
    int state=world_.block(p);
    if(simdata::lever(state)) return world_.set(p,simdata::lever_first+((state-simdata::lever_first)^1));
    if(simdata::repeater(state)) { int offset=state-simdata::repeater_first; return world_.set(p,simdata::repeater_first+((offset/16+1)%4)*16+offset%16); }
    const std::string_view name=simdata::block(state).name;
    if(name.ends_with("_door")||name.ends_with("_trapdoor")||name.ends_with("_fence_gate")) {
        if(name=="iron_door"||name=="iron_trapdoor")return true;
        const auto value=simdata::property_value(state,"open")=="true"?"false":"true";
        std::vector<std::pair<BlockPos,int>> changes{{p,simdata::with_property(state,"open",value)}};
        if(paired_vertical(state)) {
            auto other=add(p,{0,simdata::property_value(state,"half")=="lower"?1:-1,0});int s=world_.block(other);
            if(simdata::block(s).first==simdata::block(state).first&&simdata::property_value(s,"half")!=simdata::property_value(state,"half"))changes.emplace_back(other,simdata::with_property(s,"open",value));
        }
        return world_.set_blocks(changes);
    }
    return false;
}
bool Simulation::place_block(BlockPos p,int state) {
    using namespace simdata;
    if(!World::valid(p)||p.y==-64||!valid_state(state)||!state)return false;
    std::vector<std::pair<BlockPos,int>> changes;
    if(container_type(state)&&property_value(state,"type")=="single") {
        const auto f=state_direction(state);const BlockPos right{-f.z,0,f.x};
        std::vector<std::pair<BlockPos,int>> neighbors;
        for(auto direction:{right,opposite(right)}) {
            auto q=add(p,direction);int s=world_.block(q);
            if(container_type(s)==container_type(state)&&property_value(s,"facing")==property_value(state,"facing")&&property_value(s,"type")=="single")neighbors.emplace_back(q,s);
        }
        if(neighbors.size()==1) {
            const auto [q,s]=neighbors.front();const bool left=q==add(p,right);
            return world_.set_blocks({{p,with_property(state,"type",left?"left":"right")},{q,with_property(s,"type",left?"right":"left")}});
        }
    }
    if(paired_vertical(state)) {
        if(!world_.solid(add(p,{0,-1,0})))return false;
        state=with_property(state,"half","lower");
        changes={{p,state},{add(p,{0,1,0}),with_property(state,"half","upper")}};
    } else if(bed(state)) {
        state=with_property(state,"part","foot");auto head=add(p,state_direction(state));
        if(!world_.solid(add(p,{0,-1,0}))||!world_.solid(add(head,{0,-1,0})))return false;
        changes={{p,state},{head,with_property(state,"part","head")}};
    } else return world_.set(p,state);
    for(const auto& [q,s]:changes) {
        if(!World::valid(q)||!replaceable(world_.block(q)))return false;
        for(const auto* player:world_.players)if(q.x+1>player->x-.3&&q.x<player->x+.3&&q.z+1>player->z-.3&&q.z<player->z+.3&&q.y+1>player->y&&q.y<player->y+1.8)return false;
    }
    return world_.set_blocks(changes);
}
bool Simulation::break_block(BlockPos p) {
    using namespace simdata;
    const int state=world_.block(p);BlockPos other=p;bool paired=false;
    if(paired_vertical(state)){other=add(p,{0,property_value(state,"half")=="lower"?1:-1,0});paired=true;}
    else if(bed(state)){auto direction=state_direction(state);other=add(p,property_value(state,"part")=="foot"?direction:opposite(direction));paired=true;}
    std::vector<std::pair<BlockPos,int>> changes{{p,property_value(state,"waterlogged")=="true"?water_state:0}};
    const int second=world_.block(other);
    if(paired&&block(second).first==block(state).first&&
       ((paired_vertical(state)&&property_value(second,"half")!=property_value(state,"half"))||
        (bed(state)&&property_value(second,"part")!=property_value(state,"part")&&property_value(second,"facing")==property_value(state,"facing"))))
        changes.emplace_back(other,property_value(second,"waterlogged")=="true"?water_state:0);
    return world_.set_blocks(changes);
}
void Simulation::update_shapes() {
    using namespace simdata;
    for(int budget=0;budget<4096&&!shape_updates_.empty();++budget) {
        const auto p=*shape_updates_.begin();shape_updates_.erase(shape_updates_.begin());int state=world_.block(p);
        if(!connects(state))continue;
        const std::string_view name=block(state).name;const bool fence=name.ends_with("_fence");
        constexpr const char* names[]={"north","south","west","east"};
        for(int d=0;d<4;++d) {
            int n=world_.block(add(p,facing[d]));const std::string_view other=block(n).name;
            bool connected=full_cube(n);
            if(fence)connected|=(other.ends_with("_fence")&&((name=="nether_brick_fence")== (other=="nether_brick_fence")))||other.ends_with("_fence_gate");
            else connected|=other.ends_with("_pane")||other=="iron_bars";
            state=with_property(state,names[d],connected?"true":"false");
        }
        if(state!=world_.block(p))world_.set(p,state);
    }
}
void Simulation::redstone() {
    using namespace simdata;
    if(!redstone_dirty_) return;
    auto support=[&](BlockPos p,int s) {
        if(lever(s)) {int o=s-lever_first;int face=o/8;return add(p,face==0?dirs[0]:face==2?dirs[1]:opposite(facing[(o%8)/2]));}
        if(s>=redstone_wall_torch_first&&s<=redstone_wall_torch_last)return add(p,opposite(facing[(s-redstone_wall_torch_first)/2]));
        return add(p,dirs[0]);
    };
    auto emitted=[&](BlockPos from,BlockPos to,bool wires) {
        int s=world_.block(from);
        if(s==redstone_block_state)return 15;
        if(lever(s))return (s-lever_first)%2==0?15:0;
        if(torch(s))return (s-redstone_torch_first)%2==0&&to!=support(from,s)?15:0;
        if(repeater(s))return (s-repeater_first)%2==0&&to==add(from,opposite(facing[((s-repeater_first)%16)/4]))?15:0;
        return wires&&wire(s)?wire_power(s):0;
    };
    auto power=[&](BlockPos p,bool wires) {
        int best=0;
        for(auto d:dirs) {
            const auto n=add(p,d);best=std::max(best,emitted(n,p,wires));
            if(!world_.solid(n))continue;
            for(auto e:dirs) {
                const auto q=add(n,e);int s=world_.block(q);
                // Strong power into a support block; avoids wire feedback loops.
                if((lever(s)&&support(q,s)==n)||(torch(s)&&q.y==n.y-1)||repeater(s)||(wires&&wire(s)&&q.y==n.y+1))
                    best=std::max(best,emitted(q,n,wires));
            }
        }
        return best;
    };
    auto links=[&](BlockPos p) {
        std::vector<BlockPos> out;
        for(int d=2;d<6;++d) {
            auto n=add(p,dirs[d]);int s=world_.block(n);
            if(wire(s))out.push_back(n);
            else if(world_.solid(n)&&!world_.solid(add(p,dirs[1]))&&wire(world_.block(add(n,dirs[1]))))out.push_back(add(n,dirs[1]));
            else if(!world_.solid(n)&&wire(world_.block(add(n,dirs[0]))))out.push_back(add(n,dirs[0]));
        }
        return out;
    };
    std::map<BlockPos,int> strength;std::deque<BlockPos> queue;
    const auto nodes=circuits_;
    for(auto p:nodes) if(wire(world_.block(p))) {strength[p]=power(p,false);if(strength[p])queue.push_back(p);}
    while(!queue.empty()) {
        auto p=queue.front();queue.pop_front();
        for(auto n:links(p)) if(strength.contains(n)&&strength[n]<strength[p]-1) {strength[n]=strength[p]-1;queue.push_back(n);}
    }
    for(auto [p,v]:strength) {
        if(!world_.solid(add(p,dirs[0]))) {world_.set(p,0);continue;}
        // Wire property order: east, north, power, south, west.
        auto connection=[&](int d) {
            auto n=add(p,dirs[d]);
            if(circuit(world_.block(n)))return 1;
            if(world_.solid(n)&&!world_.solid(add(p,dirs[1]))&&wire(world_.block(add(n,dirs[1]))))return 0;
            if(!world_.solid(n)&&wire(world_.block(add(n,dirs[0]))))return 1;
            return 2;
        };
        int n=connection(2),s=connection(3),w=connection(4),e=connection(5);
        // A single connection renders as a line; isolated dust renders as a cross.
        const bool no_ns=n==2&&s==2,no_ew=e==2&&w==2;
        if(no_ns) {if(e==2)e=1;if(w==2)w=1;}
        if(no_ew) {if(n==2)n=1;if(s==2)s=1;}
        world_.set(p,redstone_wire_first+((((e*3+n)*16+v)*3+s)*3+w));
    }
    for(auto p:nodes) {
        const int state=world_.block(p);int next=state,delay=0;
        if(torch(state)) {
            bool lit=power(support(p,state),true)==0;
            next=state-((state-redstone_torch_first)%2)+(lit?0:1);delay=2;
        } else if(repeater(state)) {
            int off=state-repeater_first;auto f=facing[(off%16)/4];
            auto input=add(p,f);bool on=emitted(input,p,true)>0;
            if(world_.solid(input)) on=on||power(input,true)>0;
            BlockPos side{f.z,0,-f.x};bool locked=false;
            for(auto d:{side,opposite(side)}) {auto q=add(p,d);locked|=repeater(world_.block(q))&&emitted(q,p,false)>0;}
            next=repeater_first+(off/4)*4+(locked?0:2)+(locked?off%2:on?0:1);
            delay=(off/16+1)*2;
        } else if(lamp(state)) {bool on=power(p,true)>0;next=redstone_lamp_first+(on?0:1);delay=on?0:4;}
        if(next==state) {devices_.erase(p);continue;}
        if(delay==0)world_.set(p,next);
        else if(!devices_.contains(p)||devices_[p].state!=next)devices_[p]={tick+static_cast<unsigned>(delay),next};
    }
    redstone_dirty_=false;
}
int Simulation::spawn(int type,double x,double y,double z,ItemStack drop) {
    if(mobs.size()>=4096||!inside(x,y,z)||(type!=71&&type!=100&&type!=150)) return 0;
    if(type!=71&&std::count_if(mobs.begin(),mobs.end(),[](const auto& e){return e.second.type!=71;})>=128)return 0;
    Mob m; m.id=world_.next_entity++;m.type=type;m.x=x;m.y=y;m.z=z;m.health=type==150?20.f:10.f;m.born=tick;m.drop=drop;
    mobs.emplace(m.id,m);world_.mark_dirty();return m.id;
}
void Simulation::attack(GameSession& player,int entity) {
    auto found=mobs.find(entity);if(found==mobs.end()||found->second.type==71||!player.alive())return;
    auto& m=found->second;double dx=m.x-player.x,dy=m.y-player.y,dz=m.z-player.z;
    if(dx*dx+dy*dy+dz*dz>16||!player.sees(static_cast<int>(std::floor(m.x/16)),static_cast<int>(std::floor(m.z/16))))return;
    const float damage=player.attack_damage();if(damage<=0)return;
    m.health-=damage;
    if(m.health>0) {
        double length=std::hypot(dx,dz);
        if(length<1e-6) {const double angle=player.yaw*3.141592653589793/180;dx=-std::sin(angle);dz=std::cos(angle);length=1;}
        m.vx=m.vx*.5+dx/length*.4;m.vz=m.vz*.5+dz/length*.4;
        if(m.vy==0)m.vy=.4;
        m.knockback_until=tick+10;
    }
    for(auto* p:world_.players)p->mob_hurt(m.id);
    if(m.health<=0) {
        const auto dead=m;mobs.erase(found);
        spawn(71,dead.x,dead.y+.4,dead.z,{dead.type==100?simdata::porkchop_item:simdata::rotten_flesh_item,dead.type==100?2:1,0});
    }
    world_.mark_dirty();
}
void Simulation::entities() {
    std::vector<int> remove;
    for(auto& [id,m]:mobs) {
        GameSession* target=nullptr;double nearest=32*32;
        bool active=false;
        for(auto* p:world_.players) {
            active|=p->sees(static_cast<int>(std::floor(m.x/16)),static_cast<int>(std::floor(m.z/16)));
            const double d=(p->x-m.x)*(p->x-m.x)+(p->z-m.z)*(p->z-m.z);
            if(p->survival()&&p->alive()&&d<nearest) {target=p;nearest=d;}
        }
        if(!active)continue;
        if(m.type==71) {
            if(tick-m.born>6000) {remove.push_back(id);continue;}
            if(tick-m.born>=10) for(auto* p:world_.players) {
                const double d=(p->x-m.x)*(p->x-m.x)+(p->y-m.y)*(p->y-m.y)+(p->z-m.z)*(p->z-m.z);
                if(d>3||!p->alive())continue;
                int left=p->add_item(m.drop);
                if(left!=m.drop.count) {for(auto* q:world_.players)q->collect(id,p->entity_id,m.drop.count-left);m.drop.count=left;world_.mark_dirty();}
                if(!left) {remove.push_back(id);break;}
            }
        } else {
            double dx=0,dz=0,speed=m.type==150?.1:.035;
            if(m.type==150&&target&&nearest>1) {double length=std::sqrt(nearest);dx=(target->x-m.x)/length;dz=(target->z-m.z)/length;}
            else if(m.type==100 && (tick/80+id)%3!=0) {double angle=static_cast<double>((tick/100*73+id*137)%360)*3.141592653589793/180;dx=std::sin(angle);dz=std::cos(angle);}
            if(tick<m.knockback_until){dx=0;dz=0;}
            double nx=m.x+dx*speed,nz=m.z+dz*speed;
            int iy=static_cast<int>(std::floor(m.y));
            BlockPos next{static_cast<int>(std::floor(nx)),iy,static_cast<int>(std::floor(nz))};
            if(World::valid(next)&&!simdata::water(world_.block(next))&&!simdata::lava(world_.block(next))) {
                if(world_.solid(next)&&!world_.solid({next.x,iy+1,next.z})&&!world_.solid({next.x,iy+2,next.z}))m.y=iy+1;
                if(!world_.solid({next.x,static_cast<int>(std::floor(m.y)),next.z})&&!world_.solid({next.x,static_cast<int>(std::floor(m.y+ (m.type==150?1.8:.8))),next.z})) {m.x=nx;m.z=nz;}
                if(dx||dz)m.yaw=static_cast<float>(std::atan2(-dx,dz)*180/3.141592653589793);
            }
            if(m.type==150&&target&&nearest<2.5&&std::abs(m.y-target->y)<1.5&&tick>=m.next_attack) {target->damage(3,"a zombie");m.next_attack=tick+20;}
            if(m.type==150&&day_time%24000<12000&&tick%20==0) {
                bool roof=false;for(int y=static_cast<int>(m.y)+2;y<320;++y) if(world_.solid({static_cast<int>(std::floor(m.x)),y,static_cast<int>(std::floor(m.z))})){roof=true;break;}
                if(!roof)m.health-=1;
            }
            if(m.health<=0)remove.push_back(id);
        }
        // Sweep each axis in short steps, including the full body and ceiling.
        const double radius=m.type==71?.125:m.type==100?.45:.3,height=m.type==71?.25:m.type==100?.9:1.95;
        auto clear=[&](double x,double y,double z) {
            if(x-radius< -1024||x+radius>1024||z-radius< -1024||z+radius>1024||y+height>320)return false;
            for(int bx=static_cast<int>(std::floor(x-radius+1e-7));bx<=static_cast<int>(std::floor(x+radius-1e-7));++bx)
                for(int by=static_cast<int>(std::floor(y+1e-7));by<=static_cast<int>(std::floor(y+height-1e-7));++by)
                    for(int bz=static_cast<int>(std::floor(z-radius+1e-7));bz<=static_cast<int>(std::floor(z+radius-1e-7));++bz)
                        if(world_.solid({bx,by,bz}))return false;
            return true;
        };
        auto move=[&](int axis,double& velocity) {
            const int steps=std::max(1,static_cast<int>(std::ceil(std::abs(velocity)/.05)));const double delta=velocity/steps;
            for(int step=0;step<steps;++step) {
                const double nx=m.x+(axis==0?delta:0),ny=m.y+(axis==1?delta:0),nz=m.z+(axis==2?delta:0);
                if(!clear(nx,ny,nz)){velocity=0;break;}m.x=nx;m.y=ny;m.z=nz;
            }
        };
        move(0,m.vx);move(2,m.vz);
        m.vy=std::max(-1.0,(m.vy-.08)*.98);move(1,m.vy);
        const double friction=m.vy==0?.6:.91;m.vx*=friction;m.vz*=friction;
        if(std::abs(m.vx)<.003)m.vx=0;if(std::abs(m.vz)<.003)m.vz=0;
        if(m.y< -64)remove.push_back(id);
    }
    for(int id:remove)mobs.erase(id);
    if(!world_.players.empty()&&tick%200==0&&mobs.size()<32) {
        auto* player=world_.players[tick/200%world_.players.size()];
        int x=static_cast<int>(std::floor(player->x))+static_cast<int>((tick*17)%31)-15;
        int z=static_cast<int>(std::floor(player->z))+static_cast<int>((tick*29)%31)-15;
        if(player->sees(static_cast<int>(std::floor(x/16.0)),static_cast<int>(std::floor(z/16.0)))) {
            for(int y=std::min(318,static_cast<int>(player->y)+8);y>std::max(-63,static_cast<int>(player->y)-16);--y)
                if(world_.solid({x,y-1,z})&&world_.block({x,y,z})==0&&world_.block({x,y+1,z})==0) {spawn(day_time%24000>=13000?150:100,x+.5,y,z+.5);break;}
        }
    }
    if(!mobs.empty()&&!world_.players.empty())world_.mark_dirty();
}
void Simulation::step() {
    ++tick;++day_time;
    update_shapes();
    tick_furnaces();
    std::vector<BlockPos> due;
    for(const auto& [p,t]:water_)if(t<=tick&&due.size()<2048)due.push_back(p);
    for(auto p:due) {water_.erase(p);world_.mark_dirty();flow(p);}
    std::vector<std::pair<BlockPos,int>> events;
    for(const auto& [p,e]:devices_)if(e.due<=tick)events.emplace_back(p,e.state);
    for(auto [p,s]:events) {devices_.erase(p);if(simdata::circuit(world_.block(p)))world_.set(p,s);}
    redstone();entities();
    for(auto* player:world_.players)player->simulation_tick();
}
std::string Simulation::save() const {
    nlohmann::json j{{"version",1},{"tick",tick},{"time",day_time},{"water",nlohmann::json::array()},{"devices",nlohmann::json::array()},
        {"players",nlohmann::json::object()},{"mobs",nlohmann::json::array()}};
    for(auto [p,t]:water_)j["water"].push_back({p.x,p.y,p.z,t});
    for(auto [p,e]:devices_)j["devices"].push_back({p.x,p.y,p.z,e.due,e.state});
    j["containers"]=nlohmann::json::array();
    for(const auto& [p,container]:containers) {
        nlohmann::json items=nlohmann::json::array();for(auto item:container.items)items.push_back(stack_json(item));
        j["containers"].push_back({{"position",{p.x,p.y,p.z}},{"type",container.type},{"items",std::move(items)},
            {"burn_left",container.burn_left},{"burn_total",container.burn_total},{"cook_progress",container.cook_progress},{"cook_total",container.cook_total},{"cook_input",container.cook_input}});
    }
    for(const auto& [uuid,s]:saved_players) {
        auto& p=j["players"][uuid];p={{"position",{s.x,s.y,s.z}},{"health",s.health},{"food",s.food},{"saturation",s.saturation},{"exhaustion",s.exhaustion},{"mode",s.mode},{"items",nlohmann::json::array()}};
        for(auto stack:s.inventory)p["items"].push_back(stack_json(stack));
        p["cursor"]=stack_json(s.cursor);p["crafting"]=nlohmann::json::array();
        for(auto stack:s.crafting)p["crafting"].push_back(stack_json(stack));
    }
    for(const auto& [id,m]:mobs)j["mobs"].push_back({{"id",id},{"type",m.type},{"position",{m.x,m.y,m.z}},{"velocity",{m.vx,m.vy,m.vz}},{"health",m.health},{"born",m.born},{"drop",stack_json(m.drop)}});
    return j.dump();
}
void Simulation::load(std::string_view text) {
    auto j=nlohmann::json::parse(text);
    if(j.at("version")!=1)throw std::runtime_error("Unknown simulation save version");
    tick=j.at("tick").get<std::uint64_t>();day_time=j.at("time").get<std::uint64_t>();
    if(tick>1000000000000ULL||day_time>1000000000000ULL)throw std::runtime_error("Invalid simulation time");
    if(j.at("water").size()>65536||j.at("devices").size()>65536||j.at("mobs").size()>4096||j.at("players").size()>1024)throw std::runtime_error("Simulation save capacity exceeded");
    water_.clear();devices_.clear();
    containers.clear();
    if(j.contains("containers")) {
        const auto& saved=j.at("containers");if(!saved.is_array()||saved.size()>4096)throw std::runtime_error("Container save capacity exceeded");
        for(const auto& entry:saved) {
            const auto& xyz=entry.at("position");if(!xyz.is_array()||xyz.size()!=3)throw std::runtime_error("Invalid container position");
            BlockPos p{xyz[0],xyz[1],xyz[2]};Container container;container.type=entry.at("type");
            if(!World::valid(p)||!container.type||container.type!=container_type(world_.block(p))||entry.at("items").size()!=27||containers.contains(p))throw std::runtime_error("Invalid saved container");
            for(int i=0;i<27;++i)container.items[i]=read_stack(entry.at("items").at(i));
            container.burn_left=entry.value("burn_left",0);container.burn_total=entry.value("burn_total",0);
            container.cook_progress=entry.value("cook_progress",0);container.cook_total=entry.value("cook_total",200);container.cook_input=entry.value("cook_input",0);
            if(container.burn_left<0||container.burn_total<container.burn_left||container.burn_total>20000||container.cook_total<1||container.cook_total>2000||container.cook_progress<0||container.cook_progress>=container.cook_total||(container.cook_input&&!simdata::item(container.cook_input)))throw std::runtime_error("Invalid furnace progress");
            if(furnace_kind(container.type))for(int i=3;i<27;++i)if(container.items[i].item)throw std::runtime_error("Invalid hidden furnace inventory");
            containers.emplace(p,container);
        }
    }
    for(const auto& e:j.at("water")) {BlockPos p{e.at(0),e.at(1),e.at(2)};auto t=e.at(3).get<std::uint64_t>();if(!World::valid(p)||t>tick+1200)throw std::runtime_error("Invalid saved water tick");water_[p]=t;}
    for(const auto& e:j.at("devices")) {BlockPos p{e.at(0),e.at(1),e.at(2)};Event event{e.at(3),e.at(4)};if(!World::valid(p)||!simdata::circuit(event.state)||event.due>tick+1200)throw std::runtime_error("Invalid redstone tick");devices_[p]=event;}
    for(const auto& [uuid,p]:j.at("players").items()) {
        if(uuid.size()!=32||uuid.find_first_not_of("0123456789abcdef")!=std::string::npos)throw std::runtime_error("Invalid saved player UUID");
        PlayerState s;s.x=p.at("position").at(0);s.y=p.at("position").at(1);s.z=p.at("position").at(2);s.health=p.at("health");s.food=p.at("food");s.saturation=p.at("saturation");s.exhaustion=p.at("exhaustion");s.mode=p.at("mode");
        if(!inside(s.x,s.y,s.z)||!std::isfinite(s.health)||s.health<0||s.health>20||s.food<0||s.food>20||!std::isfinite(s.saturation)||s.saturation<0||s.saturation>20||!std::isfinite(s.exhaustion)||s.exhaustion<0||s.exhaustion>40||(s.mode!=0&&s.mode!=1)||p.at("items").size()!=46)throw std::runtime_error("Invalid saved player state");
        for(int i=0;i<46;++i)s.inventory[i]=read_stack(p["items"][i]);
        if(p.contains("cursor"))s.cursor=read_stack(p.at("cursor"));
        if(p.contains("crafting")){if(p.at("crafting").size()!=9)throw std::runtime_error("Invalid saved crafting grid");for(int i=0;i<9;++i)s.crafting[i]=read_stack(p.at("crafting").at(i));}
        saved_players.emplace(uuid,s);
    }
    for(const auto& p:j.at("mobs")) {
        Mob m;m.id=p.at("id");m.type=p.at("type");m.x=p.at("position").at(0);m.y=p.at("position").at(1);m.z=p.at("position").at(2);m.health=p.at("health");m.born=p.at("born");m.drop=read_stack(p.at("drop"));
        if(p.contains("velocity")) {
            const auto& v=p.at("velocity");if(!v.is_array()||v.size()!=3)throw std::runtime_error("Invalid mob velocity");
            m.vx=v[0];m.vy=v[1];m.vz=v[2];
            if(!std::isfinite(m.vx)||!std::isfinite(m.vy)||!std::isfinite(m.vz)||std::abs(m.vx)>4||std::abs(m.vy)>4||std::abs(m.vz)>4)throw std::runtime_error("Invalid mob velocity");
        }
        if(m.id<1||m.id>100000000||!inside(m.x,m.y,m.z)||(m.type!=71&&m.type!=100&&m.type!=150)||!std::isfinite(m.health)||m.health<=0||m.health>20||m.born>tick||(m.type==71&&!m.drop.item)||mobs.contains(m.id))throw std::runtime_error("Invalid saved mob");
        mobs.emplace(m.id,m);world_.next_entity=std::max(world_.next_entity,m.id+1);
    }
    redstone_dirty_=true;
}
}
