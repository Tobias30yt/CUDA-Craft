#include "cudacraft/simulation.hpp"
#include "cudacraft/game.hpp"
#include "cudacraft/simulation_data.hpp"
#include "cudacraft/world.hpp"
#include <algorithm>
#include <cmath>
#include <numbers>

namespace cudacraft {
namespace {
bool tnt_block(int state) { return std::string_view(simdata::block(state).name)=="tnt"; }

std::uint64_t mix(std::uint64_t value) {
    value=(value^(value>>30))*0xbf58476d1ce4e5b9ULL;
    value=(value^(value>>27))*0x94d049bb133111ebULL;
    return value^(value>>31);
}
double unit(std::uint64_t value) { return (mix(value)>>11)*0x1.0p-53; }
BlockPos position(double x,double y,double z) {
    return {static_cast<int>(std::floor(x)),static_cast<int>(std::floor(y)),static_cast<int>(std::floor(z))};
}
bool valid_tnt(const PrimedTnt& tnt) {
    return std::isfinite(tnt.x)&&std::isfinite(tnt.y)&&std::isfinite(tnt.z)&&
        std::abs(tnt.x)<1024&&std::abs(tnt.z)<1024&&tnt.y>=-64&&tnt.y<400&&
        std::isfinite(tnt.vx)&&std::isfinite(tnt.vy)&&std::isfinite(tnt.vz)&&
        std::abs(tnt.vx)<=1024&&std::abs(tnt.vy)<=1024&&std::abs(tnt.vz)<=1024&&tnt.fuse>=1&&tnt.fuse<=32767;
}

double clip(const World& world,const PrimedTnt& tnt,int axis,double distance) {
    constexpr double height=.98f,radius=height/2;
    return world.clip_motion({tnt.x-radius,tnt.y,tnt.z-radius},
                             {tnt.x+radius,tnt.y+height,tnt.z+radius},axis,distance);
}
}

int Simulation::spawn_tnt(PrimedTnt tnt) {
    if(primed_tnt.size()>=4096||!valid_tnt(tnt))return 0;
    const int id=world_.root().next_entity++;
    primed_tnt.emplace(id,tnt);
    world_.mark_dirty();
    return id;
}

bool Simulation::ignite_tnt(BlockPos p,int fuse) {
    if(!tnt_block(world_.block(p)))return false;
    const double angle=unit(packed_position(p)^tick^static_cast<std::uint64_t>(world_.options().seed))*2*std::numbers::pi;
    const auto id=spawn_tnt({p.x+.5,static_cast<double>(p.y),p.z+.5,-std::sin(angle)*.02,.20000000298023224,
                            -std::cos(angle)*.02,fuse});
    if(!id)return false;
    if(world_.set(p,0))return true;
    primed_tnt.erase(id);
    return false;
}

void Simulation::update_tnt(BlockPos changed) {
    if(tnt_blocks_.empty())return;
    constexpr BlockPos offsets[]={{0,0,0},{0,-1,0},{0,1,0},{0,0,-1},{0,0,1},{-1,0,0},{1,0,0}};
    for(auto offset:offsets) {
        const BlockPos p{changed.x+offset.x,changed.y+offset.y,changed.z+offset.z};
        if(tnt_blocks_.contains(p)&&neighbor_power(p,true)>0)ignite_tnt(p);
    }
}

void Simulation::tick_tnt() {
    if(primed_tnt.empty())return;
    std::vector<int> ids;
    for(const auto& [id,tnt]:primed_tnt){(void)tnt;ids.push_back(id);}
    for(const int id:ids) {
        const auto found=primed_tnt.find(id);
        if(found==primed_tnt.end())continue;
        auto& tnt=found->second;
        tnt.vy-=.04;
        const double dy=clip(world_,tnt,1,tnt.vy);
        tnt.y+=dy;
        const bool grounded=dy!=tnt.vy&&tnt.vy<0;
        auto horizontal=[&](int axis,double& coordinate,double& velocity) {
            const double moved=clip(world_,tnt,axis,velocity);
            coordinate+=moved;
            if(moved!=velocity)velocity=0;
        };
        if(std::abs(tnt.vx)<std::abs(tnt.vz)) {
            horizontal(2,tnt.z,tnt.vz);
            horizontal(0,tnt.x,tnt.vx);
        } else {
            horizontal(0,tnt.x,tnt.vx);
            horizontal(2,tnt.z,tnt.vz);
        }
        tnt.vx*=.98; tnt.vy*=.98; tnt.vz*=.98;
        if(grounded){tnt.vx*=.7;tnt.vy=0;tnt.vz*=.7;}
        if(--tnt.fuse==0) {
            const auto source=tnt;
            primed_tnt.erase(found);
            explode_tnt(id,source);
        } else if(!valid_tnt(tnt))primed_tnt.erase(found);
    }
    world_.mark_dirty();
}

void Simulation::explode_tnt(int id,const PrimedTnt& source) {
    using namespace simdata;
    const double x=source.x,y=source.y+.06125,z=source.z;
    std::set<BlockPos> destroyed;
    std::uint64_t random=static_cast<std::uint64_t>(world_.options().seed)^tick^static_cast<std::uint64_t>(id);
    for(int a=0;a<16;++a)for(int b=0;b<16;++b)for(int c=0;c<16;++c) {
        if(a&&a!=15&&b&&b!=15&&c&&c!=15)continue;
        double dx=a/7.5-1,dy=b/7.5-1,dz=c/7.5-1;
        const double length=std::hypot(dx,dy,dz);
        dx*=.3/length;dy*=.3/length;dz*=.3/length;
        double power=4*(.7+unit(++random)*.6);
        double px=x,py=y,pz=z;
        while(power>0) {
            const auto p=position(px,py,pz);
            if(!World::valid(p))break;
            const int state=world_.block(p);
            if(state!=0) {
                const auto resistance=blast_resistance(state);
                power-=(resistance+.3)*.3;
                if(power>0&&block(state).hardness>=0&&p.y>world_.min_y())destroyed.insert(p);
            }
            px+=dx;py+=dy;pz+=dz;
            power-=.225;
        }
    }
    auto impulse=[&](double px,double py,double pz,double& vx,double& vy,double& vz) {
        const double dx=px-x,dy=py-y,dz=pz-z,distance=std::hypot(dx,dy,dz);
        if(distance<=1e-7||distance>=8)return;
        for(double step=.3;step<distance;step+=.3)
            if(world_.solid(position(x+dx*step/distance,y+dy*step/distance,z+dz*step/distance)))return;
        const double scale=(1-distance/8)/distance;
        vx+=dx*scale;vy+=dy*scale;vz+=dz*scale;
    };
    for(auto& [other,tnt]:primed_tnt){(void)other;impulse(tnt.x,tnt.y+.49,tnt.z,tnt.vx,tnt.vy,tnt.vz);}
    std::vector<std::pair<BlockPos,int>> changes;
    std::vector<BlockPos> chain;
    for(auto p:destroyed) {
        if(tnt_block(world_.block(p)))chain.push_back(p);
        else changes.emplace_back(p,0);
    }
    if(!changes.empty())world_.set_blocks(changes);
    for(auto p:chain)ignite_tnt(p,10+static_cast<int>(unit(++random)*20));
    for(auto* player:world_.players)if(std::hypot(player->x-x,player->y-y,player->z-z)<64)
        player->explosion(x,y,z,4,static_cast<int>(destroyed.size()));
}
}
