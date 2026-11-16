#include "cudacraft/biome_source.hpp"
#include "cudacraft/noise_terrain.hpp"
#include <charconv>
#include <fstream>
#include <iostream>
#include <string>
int main(int argc,char** argv) {
    try {
        if(argc!=5 && argc!=6) throw std::runtime_error("Usage: terrain-probe SEED CX CZ OUTPUT.u32le [--no-carvers]");
        if(argc==6 && std::string(argv[5])!="--no-carvers") throw std::runtime_error("Unknown probe option");
        cudacraft::BiomeSource biomes(std::stoll(argv[1]));
        cudacraft::NoiseTerrain terrain(biomes);
        auto c=terrain.chunk(std::stoi(argv[2]),std::stoi(argv[3]),argc==5);
        std::ofstream out(argv[4],std::ios::binary);
        for(auto state:c.blocks) for(int i=0;i<4;++i) out.put(static_cast<char>(state>>(8*i)));
        if(!out) throw std::runtime_error("Cannot write terrain probe");
        std::cout<<"Generated "<<c.blocks.size()<<" blocks\n";
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
