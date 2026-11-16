#pragma once
#include <string>
#include <vector>
#include <cstdint>
namespace cudacraft {
// Polled on the server thread: no detached reader, blocking getline or shutdown race.
class ConsoleInput {
public:
    ConsoleInput();
    ~ConsoleInput();
    std::vector<std::string> poll();
    bool ended() const{return ended_;}
private:
    void feed(char c,std::vector<std::string>& lines);
    std::string line_;
    std::wstring wide_line_;
    bool ended_=false,discard_=false,is_console_=false;
    void* handle_=nullptr;
    std::uint32_t original_mode_=0;
};
}
