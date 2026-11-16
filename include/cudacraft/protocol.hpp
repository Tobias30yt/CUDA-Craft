#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace cudacraft::protocol {
using Bytes = std::vector<std::uint8_t>;
inline constexpr std::int32_t target_protocol = 774;
inline constexpr std::string_view target_version = "1.21.11";
// This endpoint only accepts small handshake/status packets, not play packets.
inline constexpr std::size_t max_frame_size = 4096;
inline constexpr std::size_t max_login_payload = 65536;

struct Error : std::runtime_error { using std::runtime_error::runtime_error; };
struct VarInt { std::int32_t value; std::size_t size; };
std::optional<VarInt> decode_varint(std::span<const std::uint8_t> bytes, std::size_t max_bytes = 5);
void append_varint(Bytes& out, std::int32_t value);
void append_string(Bytes& out, std::string_view text);
Bytes frame(std::span<const std::uint8_t> payload);
std::optional<Bytes> take_frame(Bytes& buffer, std::size_t limit = max_frame_size);
std::string json_quote(std::string_view text);

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::int32_t varint();
    std::uint16_t u16();
    std::string string(std::size_t max_utf16_units);
    std::span<const std::uint8_t> bytes(std::size_t count);
    std::span<const std::uint8_t> rest() const { return bytes_.subspan(offset_); }
    void require_end() const;
private:
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
};

struct Status {
    std::string motd = "CUDA Craft | 1.21.11 | Survival, water, mobs and redstone";
    std::int32_t max_players = 20;
    std::int32_t online_players = 0;
    std::string json() const;
};
struct Reply { Bytes bytes; bool close = false; };
class Session {
public:
    Reply handle(std::span<const std::uint8_t> payload, const Status& status);
    bool wants_login() const { return state_ == State::login; }
private:
    enum class State { handshake, status, login, closed };
    State state_ = State::handshake;
    bool status_sent_ = false;
};
}
