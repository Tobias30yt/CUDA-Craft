#include "cudacraft/server.hpp"
#include "cudacraft/gpu.hpp"
#include <charconv>
#include <iostream>
#include <string_view>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace {
volatile std::sig_atomic_t stopping = 0;
void stop_handler(int) { stopping = 1; }
int number(std::string_view value, int low, int high) {
    int parsed = 0;
    const auto result = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (result.ec != std::errc{} || result.ptr != value.data() + value.size() || parsed < low || parsed > high) throw std::runtime_error("Invalid numeric argument: " + std::string(value));
    return parsed;
}
}
int run(const std::vector<std::string>& args) {
    try {
        cudacraft::ServerConfig config;
        config.terrain.generator="density";
        config.terrain.seed=20261113;
        config.terrain.gamemode=0;
        bool explicit_world=false,local_session=false;
        for (std::size_t i = 1; i < args.size(); ++i) {
            const std::string_view option(args[i]);
            if (option == "--help") {
                std::cout << "CUDA Craft 0.11.6 - Minecraft Java 1.21.11 (protocol 774)\n"
                    "Survival prototype with water, basic mobs and redstone. Vanilla parity is incomplete.\n"
                    "  --bind IPv4             Bind address (default 127.0.0.1)\n"
                    "  --port 0..65535         TCP port (default 25565; 0 selects a free port)\n"
                    "  --motd TEXT             UTF-8 server description (max 2048 bytes)\n"
                    "  --max-players N         Advertised capacity (default 20)\n"
                    "  --world PATH            Save directory (default world-density)\n"
                    "  --generator TYPE        density (default), climate (0.5), overworld (0.4), flat\n"
                    "  --seed INTEGER          Signed 64-bit seed (new worlds default 20261113)\n"
                    "  --gamemode MODE         survival (default) or creative for new players\n"
                    "  --local-session         Private Fabric child process; requires session environment\n"
                    "  --max-connections N     Concurrent status connections (1..32)\n"
                    "  --timeout-ms N          Status deadline (default 5000); login deadline is 30000\n"
                    "  --run-for-ms N          Stop after N milliseconds (default 0: unlimited)\n"
                    "  --gpu-self-test         Run CUDA diagnostic and exit\n"
                    "  --version               Print version and exit\n"
                    "Console: help, op <player>, perf, monitor on, save-all, stop\n";
                return 0;
            }
            if (option == "--version") { std::cout << "CUDA Craft 0.11.6 | Minecraft Java 1.21.11 | protocol 774\n"; return 0; }
            if (option == "--gpu-self-test") {
                std::string detail;
                const int result = cudacraft::gpu::self_test(detail);
                std::cout << detail << '\n';
                return result;
            }
            if(option=="--local-session"){local_session=true;continue;}
            if (i + 1 >= args.size()) throw std::runtime_error("Missing value for " + std::string(option));
            const std::string_view value(args[++i]);
            if (option == "--bind") config.bind_address = value;
            else if(option=="--gamemode") {if(value!="survival"&&value!="creative")throw std::runtime_error("Gamemode must be survival or creative");config.terrain.gamemode=value=="creative"?1:0;}
            else if (option == "--world") { config.world_directory = std::filesystem::path(std::u8string(value.begin(),value.end())); explicit_world=true; }
            else if (option == "--generator") {
                if(value!="flat" && value!="overworld" && value!="climate" && value!="density") throw std::runtime_error("Generator must be flat, overworld, climate or density");
                config.terrain.generator=value; config.terrain.explicit_generator=true;
            }
            else if (option == "--seed") {
                const auto parsed=std::from_chars(value.data(),value.data()+value.size(),config.terrain.seed);
                if(parsed.ec!=std::errc{} || parsed.ptr!=value.data()+value.size()) throw std::runtime_error("Seed must be a signed 64-bit integer");
                config.terrain.explicit_seed=true;
            }
            else if (option == "--port") config.port = static_cast<std::uint16_t>(number(value, 0, 65535));
            else if (option == "--motd") config.status.motd = value;
            else if (option == "--max-players") config.status.max_players = number(value, 0, 1000000);
            else if (option == "--max-connections") config.max_connections = static_cast<std::size_t>(number(value, 1, 32));
            else if (option == "--timeout-ms") config.timeout_ms = number(value, 1, 60000);
            else if (option == "--run-for-ms") config.run_for_ms = number(value, 0, 2147483647);
            else throw std::runtime_error("Unknown option: " + std::string(option));
        }
        if(!explicit_world) config.world_directory=config.terrain.generator=="flat"?"world":"world-"+config.terrain.generator;
        if(local_session) {
#ifdef CUDACRAFT_LOGIN
            if(config.bind_address!="127.0.0.1")throw std::runtime_error("Local sessions cannot listen on a public address");
            if(!explicit_world)throw std::runtime_error("Local sessions require their own --world directory");
            auto environment=[](const char* name){char buffer[256]{};DWORD size=GetEnvironmentVariableA(name,buffer,sizeof(buffer));if(!size||size>=sizeof(buffer))throw std::runtime_error("Missing private-session environment");std::string value(buffer,size);SetEnvironmentVariableA(name,nullptr);SecureZeroMemory(buffer,sizeof(buffer));return value;};
            auto hex=[](std::string_view s,std::span<std::uint8_t> output){if(s.size()!=output.size()*2)throw std::runtime_error("Invalid private-session identity");for(std::size_t i=0;i<output.size();++i){unsigned value=0;auto r=std::from_chars(s.data()+i*2,s.data()+i*2+2,value,16);if(r.ec!=std::errc{}||r.ptr!=s.data()+i*2+2)throw std::runtime_error("Invalid private-session identity");output[i]=static_cast<std::uint8_t>(value);}};
            cudacraft::auth::LocalSession session;session.owner.name=environment("CUDACRAFT_LOCAL_NAME");
            if(!cudacraft::auth::valid_username(session.owner.name))throw std::runtime_error("Invalid local player name");
            auto identity=environment("CUDACRAFT_LOCAL_UUID");hex(identity,session.owner.uuid);
            auto secret=environment("CUDACRAFT_LOCAL_TOKEN");hex(secret,session.secret);SecureZeroMemory(secret.data(),secret.size());
            config.local_session=session;config.port=0;
#else
            throw std::runtime_error("Private sessions currently require the Windows build");
#endif
        }
        std::signal(SIGINT, stop_handler);
        std::signal(SIGTERM, stop_handler);
#ifndef _WIN32
        std::signal(SIGPIPE, SIG_IGN);
#endif
        std::cout << "CUDA backend: " << (cudacraft::gpu::compiled() ? "compiled (legacy terrain kernels); density generator currently uses CPU" : "not compiled; CPU terrain") << std::endl;
        cudacraft::run_server(config, stopping);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "CUDA Craft: " << error.what() << '\n';
        return 1;
    }
}

#ifdef _WIN32
// Windows argv uses a legacy code page; decode the native UTF-16 command line instead.
int wmain(int argc, wchar_t** argv) {
    try {
        std::vector<std::string> args;
        for (int i = 0; i < argc; ++i) {
            const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, nullptr, 0, nullptr, nullptr);
            if (size == 0) throw std::runtime_error("Invalid Unicode command line");
            std::string value(static_cast<std::size_t>(size), '\0');
            if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, argv[i], -1, value.data(), size, nullptr, nullptr) == 0)
                throw std::runtime_error("Cannot decode command line");
            value.pop_back();
            args.push_back(std::move(value));
        }
        return run(args);
    } catch (const std::exception& error) {
        std::cerr << "CUDA Craft: " << error.what() << '\n';
        return 1;
    }
}
#else
int main(int argc, char** argv) { return run(std::vector<std::string>(argv, argv + argc)); }
#endif
