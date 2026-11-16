// Test-only native reference probe; no server or account authentication path.
#include "cudacraft/biome_source.hpp"
#include <nlohmann/json.hpp>
#include <iostream>

int main(int argc,char** argv) {
    try {
        if(argc<4 || (argc-2)%2) throw std::runtime_error("Usage: biome-probe seed cx cz [cx cz ...]");
        cudacraft::BiomeSource source(std::stoll(argv[1]));
        nlohmann::json out{{"seed",source.seed()},{"seed_hash",source.seed_hash()},{"chunks",nlohmann::json::array()}};
        for(int i=2;i+1<argc;i+=2) {
            const int x=std::stoi(argv[i]),z=std::stoi(argv[i+1]);
            out["chunks"].push_back({{"x",x},{"z",z},{"biomes",source.chunk(x,z)}});
        }
        std::cout<<out.dump()<<'\n';
        return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<'\n'; return 1; }
}
