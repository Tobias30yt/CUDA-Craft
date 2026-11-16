#include "cudacraft/protocol.hpp"
#include <bit>
#include <limits>

namespace cudacraft::protocol {
namespace {
std::size_t utf16_length(std::string_view text) {
    std::size_t units = 0;
    for (std::size_t i = 0; i < text.size();) {
        const auto first = static_cast<unsigned char>(text[i++]);
        std::uint32_t cp = first;
        unsigned count = 0;
        std::uint32_t minimum = 0;
        if (first >= 0xc2 && first <= 0xdf) { cp = first & 0x1f; count = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { cp = first & 0x0f; count = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { cp = first & 7; count = 3; minimum = 0x10000; }
        else if (first >= 0x80) { throw Error("Invalid UTF-8"); }
        for (unsigned j = 0; j < count; ++j) {
            if (i == text.size()) throw Error("Truncated UTF-8");
            const auto next = static_cast<unsigned char>(text[i++]);
            if ((next & 0xc0) != 0x80) throw Error("Invalid UTF-8 continuation");
            cp = (cp << 6) | (next & 0x3f);
        }
        if (cp < minimum || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) throw Error("Invalid UTF-8 codepoint");
        units += cp > 0xffff ? 2 : 1;
    }
    return units;
}
Bytes string_packet(std::string_view text) {
    Bytes payload{0};
    append_string(payload, text);
    return frame(payload);
}
}

std::optional<VarInt> decode_varint(std::span<const std::uint8_t> bytes, std::size_t max_bytes) {
    if (max_bytes == 0 || max_bytes > 5) throw Error("Invalid VarInt limit");
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < max_bytes; ++i) {
        if (i == bytes.size()) return std::nullopt;
        const auto b = bytes[i];
        if (i == 4 && (b & 0xf0) != 0) throw Error("VarInt overflow");
        value |= static_cast<std::uint32_t>(b & 0x7f) << (7 * i);
        if ((b & 0x80) == 0) return VarInt{std::bit_cast<std::int32_t>(value), i + 1};
    }
    throw Error("VarInt too long");
}
void append_varint(Bytes& out, std::int32_t value) {
    auto bits = std::bit_cast<std::uint32_t>(value);
    do {
        auto byte = static_cast<std::uint8_t>(bits & 0x7f);
        bits >>= 7;
        out.push_back(static_cast<std::uint8_t>(byte | (bits ? 0x80 : 0)));
    } while (bits);
}
void append_string(Bytes& out, std::string_view text) {
    if (text.size() > static_cast<std::size_t>(std::numeric_limits<std::int32_t>::max())) throw Error("String too large");
    append_varint(out, static_cast<std::int32_t>(text.size()));
    out.insert(out.end(), text.begin(), text.end());
}
Bytes frame(std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > 0x1fffff) throw Error("Invalid outgoing frame size");
    Bytes result;
    append_varint(result, static_cast<std::int32_t>(payload.size()));
    result.insert(result.end(), payload.begin(), payload.end());
    return result;
}
std::optional<Bytes> take_frame(Bytes& buffer, std::size_t limit) {
    const auto length = decode_varint(buffer, 3);
    if (!length) return std::nullopt;
    if (length->value <= 0 || static_cast<std::size_t>(length->value) > limit) throw Error("Invalid incoming frame size");
    const auto end = length->size + static_cast<std::size_t>(length->value);
    if (buffer.size() < end) return std::nullopt;
    Bytes result(buffer.begin() + static_cast<std::ptrdiff_t>(length->size), buffer.begin() + static_cast<std::ptrdiff_t>(end));
    buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(end));
    return result;
}
std::int32_t Reader::varint() {
    const auto value = decode_varint(rest());
    if (!value) throw Error("Truncated VarInt");
    offset_ += value->size;
    return value->value;
}
std::uint16_t Reader::u16() {
    if (rest().size() < 2) throw Error("Truncated unsigned short");
    const auto value = static_cast<std::uint16_t>((bytes_[offset_] << 8) | bytes_[offset_ + 1]);
    offset_ += 2;
    return value;
}
std::string Reader::string(std::size_t max_utf16_units) {
    const auto size = varint();
    if (size < 0 || static_cast<std::size_t>(size) > max_utf16_units * 3 || static_cast<std::size_t>(size) > rest().size()) throw Error("Invalid string length");
    std::string value(reinterpret_cast<const char*>(bytes_.data() + offset_), static_cast<std::size_t>(size));
    offset_ += static_cast<std::size_t>(size);
    if (utf16_length(value) > max_utf16_units) throw Error("String character limit exceeded");
    return value;
}
void Reader::require_end() const { if (!rest().empty()) throw Error("Unexpected trailing packet data"); }
std::span<const std::uint8_t> Reader::bytes(std::size_t count) {
    if (count > rest().size()) throw Error("Truncated byte field");
    const auto result = rest().first(count);
    offset_ += count;
    return result;
}
std::string json_quote(std::string_view text) {
    (void)utf16_length(text);
    constexpr char hex[] = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char c : text) {
        if (c == '"' || c == '\\') { out += '\\'; out += static_cast<char>(c); }
        else if (c < 0x20) { out += "\\u00"; out += hex[c >> 4]; out += hex[c & 15]; }
        else out += static_cast<char>(c);
    }
    return out + '"';
}
std::string Status::json() const {
    if (motd.size() > 2048 || max_players < 0) throw Error("Invalid status settings");
    return "{\"version\":{\"name\":\"CUDA Craft " + std::string(target_version) + "\",\"protocol\":" + std::to_string(target_protocol)
        + "},\"players\":{\"max\":" + std::to_string(max_players) + ",\"online\":" + std::to_string(online_players) + ",\"sample\":[]},\"description\":{\"text\":"
        + json_quote(motd) + "},\"enforcesSecureChat\":false}";
}
Reply Session::handle(std::span<const std::uint8_t> payload, const Status& status) {
    Reader reader(payload);
    const auto id = reader.varint();
    if (state_ == State::handshake) {
        if (id != 0) throw Error("Expected handshake");
        const auto client_version = reader.varint(); // Status remains version independent.
        (void)reader.string(255);
        (void)reader.u16();
        const auto next = reader.varint();
        reader.require_end();
        if (next == 1) { state_ = State::status; return {}; }
        if (next == 2) {
            if (client_version != target_protocol) {
                state_ = State::closed;
                return {string_packet("{\"text\":\"CUDA Craft requires Minecraft Java 1.21.11 (protocol 774).\"}"), true};
            }
            state_ = State::login;
            return {};
        }
        throw Error("Unsupported handshake state");
    }
    if (state_ != State::status) throw Error("Session is closed");
    if (id == 0 && !status_sent_) {
        reader.require_end();
        status_sent_ = true;
        return {string_packet(status.json()), false};
    }
    if (id == 1 && reader.rest().size() == 8) {
        Bytes pong{1};
        pong.insert(pong.end(), reader.rest().begin(), reader.rest().end());
        state_ = State::closed;
        return {frame(pong), true};
    }
    throw Error("Unexpected status packet");
}
}
