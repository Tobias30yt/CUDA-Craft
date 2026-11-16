#pragma once
#include "cudacraft/protocol.hpp"
#include <bit>

namespace cudacraft {
struct BlockPos {
    int x, y, z;
    auto operator<=>(const BlockPos&) const = default;
};
inline std::uint64_t packed_position(BlockPos p) {
    return (static_cast<std::uint64_t>(p.x) & 0x3ffffff) << 38 |
           (static_cast<std::uint64_t>(p.z) & 0x3ffffff) << 12 |
           (static_cast<std::uint64_t>(p.y) & 0xfff);
}
inline BlockPos unpack_position(std::uint64_t value) {
    int x = static_cast<int>(value >> 38), z = static_cast<int>((value >> 12) & 0x3ffffff), y = static_cast<int>(value & 0xfff);
    if (x & 0x2000000) x -= 0x4000000;
    if (z & 0x2000000) z -= 0x4000000;
    if (y & 0x800) y -= 0x1000;
    return {x,y,z};
}
struct Packet {
    protocol::Bytes data;
    explicit Packet(int id = -1) { if (id >= 0) var(id); }
    Packet& var(std::int32_t v) { protocol::append_varint(data, v); return *this; }
    Packet& u8(std::uint8_t v) { data.push_back(v); return *this; }
    Packet& u16(std::uint16_t v) { return u8(static_cast<std::uint8_t>(v >> 8)).u8(static_cast<std::uint8_t>(v)); }
    Packet& u32(std::uint32_t v) { return u16(static_cast<std::uint16_t>(v >> 16)).u16(static_cast<std::uint16_t>(v)); }
    Packet& u64(std::uint64_t v) { return u32(static_cast<std::uint32_t>(v >> 32)).u32(static_cast<std::uint32_t>(v)); }
    Packet& f32(float v) { return u32(std::bit_cast<std::uint32_t>(v)); }
    Packet& f64(double v) { return u64(std::bit_cast<std::uint64_t>(v)); }
    Packet& str(std::string_view v) { protocol::append_string(data,v); return *this; }
    Packet& raw(std::span<const std::uint8_t> v) { data.insert(data.end(),v.begin(),v.end()); return *this; }
    Packet& pos(BlockPos p) { return u64(packed_position(p)); }
    Packet& text(std::string_view v) {
        u8(10).u8(8).u16(4); raw(protocol::Bytes{'t','e','x','t'});
        u16(static_cast<std::uint16_t>(v.size())); data.insert(data.end(),v.begin(),v.end()); return u8(0);
    }
};
inline std::uint64_t read_u64(protocol::Reader& reader) {
    std::uint64_t result=0;
    for (auto b:reader.bytes(8)) result=(result<<8)|b;
    return result;
}
inline std::uint32_t read_u32(protocol::Reader& reader) {
    std::uint32_t result=0;
    for (auto b:reader.bytes(4)) result=(result<<8)|b;
    return result;
}
inline double read_f64(protocol::Reader& r) { return std::bit_cast<double>(read_u64(r)); }
inline float read_f32(protocol::Reader& r) { return std::bit_cast<float>(read_u32(r)); }
}
