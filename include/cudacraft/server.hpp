#pragma once
#include "cudacraft/protocol.hpp"
#include <csignal>
#include <filesystem>
#include "cudacraft/terrain.hpp"
#ifdef CUDACRAFT_LOGIN
#include "cudacraft/auth.hpp"
#endif

namespace cudacraft {
class World;
struct ServerConfig {
    std::string bind_address = "127.0.0.1";
    std::uint16_t port = 25565;
    std::size_t max_connections = 32;
    int timeout_ms = 5000;
    int run_for_ms = 0;
    protocol::Status status;
    std::filesystem::path world_directory="world";
    TerrainOptions terrain;
#ifdef CUDACRAFT_LOGIN
    // Injectable for the test fixture; normal network sessions use Mojang.
    // Private local sessions instead prove possession of an owner-bound secret.
    auth::Verifier verifier = auth::verify_mojang;
    bool authenticate_clients = true;
    std::optional<auth::LocalSession> local_session;
#endif
};
void run_server(const ServerConfig& config, const volatile std::sig_atomic_t& stop);
}
