#pragma once
#include "cudacraft/protocol.hpp"
#include <array>
#include <functional>
#include <future>
#include <memory>

namespace cudacraft::auth {
struct Property { std::string name, value; std::optional<std::string> signature; };
struct Profile {
    std::array<std::uint8_t, 16> uuid{};
    std::string name;
    std::vector<Property> properties;
};
// A private child-process session, never an unauthenticated public server mode.
struct LocalSession {
    Profile owner;
    std::array<std::uint8_t,32> secret{};
};
bool valid_username(std::string_view name);
Profile parse_profile(std::string_view json, std::string_view requested_name);
Profile verify_mojang(const std::string& name, const std::string& server_hash);
using Verifier = std::function<Profile(const std::string&, const std::string&)>;
class Pool {
public:
    explicit Pool(Verifier verifier = verify_mojang);
    ~Pool();
    std::future<Profile> submit(std::string name, std::string hash);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
