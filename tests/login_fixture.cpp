// Test-only executable. Never installed; production has no offline/mock-auth switch.
#include "cudacraft/server.hpp"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc,char** argv) {
    try {
        cudacraft::ServerConfig config;
        config.bind_address = "127.0.0.1";
        config.port = 0;
        config.run_for_ms = 60000;
        config.world_directory.clear();
        if(argc>1 && std::string(argv[1])=="--visual") {
            config.authenticate_clients=false;
            config.port=25566;
            config.run_for_ms=0;
        }
        for(int i=1;i+1<argc;++i) {
            if(std::string(argv[i])=="--world") config.world_directory=argv[++i];
            else if(std::string(argv[i])=="--generator") { config.terrain.generator=argv[++i]; config.terrain.explicit_generator=true; }
            else if(std::string(argv[i])=="--seed") { config.terrain.seed=std::stoll(argv[++i]); config.terrain.explicit_seed=true; }
            else if(std::string(argv[i])=="--gamemode") config.terrain.gamemode=std::string(argv[++i])=="survival"?0:1;
        }
        config.verifier = [](const std::string& name, const std::string& hash) {
            if (hash.empty() || hash.size() > 41) throw cudacraft::protocol::Error("Invalid session hash");
            if (name == "RejectMe") throw cudacraft::protocol::Error("Test authentication rejection");
            if (name == "SlowAuth") std::this_thread::sleep_for(std::chrono::milliseconds(1500));
            cudacraft::auth::Profile profile;
            profile.name = name;
            for (unsigned i = 0; i < 16; ++i) profile.uuid[i] = static_cast<std::uint8_t>(i);
            if(name=="SecondTester") profile.uuid[15]=16;
            if (name == "Mismatch") profile.uuid[0] = 0xff;
            if(name!="GuiTester") profile.properties.push_back({"textures", std::string(600, 'x'), "test-signature"});
            return profile;
        };
        volatile std::sig_atomic_t stop = 0;
        std::cout << "TEST FIXTURE ONLY - mocked account verification" << std::endl;
        cudacraft::run_server(config, stop);
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
