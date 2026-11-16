#pragma once
#include "cudacraft/auth.hpp"
#include <chrono>
#include <filesystem>
#include <functional>
#include <map>
#include <string>
#include <vector>
namespace cudacraft {
class World;
class GameSession;
class Administration {
public:
    using Reply=std::function<void(const std::string&)>;
    Administration(World& world,std::filesystem::path directory);
    void joined(const auth::Profile& profile);
    bool is_operator(const auth::Profile& profile) const;
    void execute(std::string command,GameSession* sender,Reply reply);
    std::vector<std::string> commands(GameSession* sender) const;
    std::vector<std::string> suggestions(std::string_view prefix,GameSession* sender) const;
    void poll();
    std::string performance_json() const;
    bool stopping=false;
private:
    World& world_;
    std::filesystem::path directory_;
    std::map<std::string,std::string> operators_,known_;
    bool console_monitor_=false;
    int monitor_seconds_=5;
    std::chrono::steady_clock::time_point last_console_=std::chrono::steady_clock::now(),last_hud_=last_console_;
    void persist() const;
    GameSession* player(std::string_view name,GameSession* sender) const;
};
}
