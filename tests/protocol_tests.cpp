#include "cudacraft/protocol.hpp"
#include <functional>
#include <iostream>
#include <limits>

using namespace cudacraft::protocol;
namespace {
int checks = 0;
void require(bool ok, const char* message) {
    ++checks;
    if (!ok) throw std::runtime_error(message);
}
void rejects(const std::function<void()>& fn) {
    bool rejected = false;
    try { fn(); } catch (const Error&) { rejected = true; }
    require(rejected, "Expected a protocol error");
}
Bytes handshake(int state = 1, int version = 774) {
    Bytes packet{0};
    append_varint(packet, version);
    append_string(packet, "localhost");
    packet.insert(packet.end(), {0x63, 0xdd});
    append_varint(packet, state);
    return packet;
}
}
int main() {
    try {
        for (auto value : {0, 1, 127, 128, 255, 2147483647, -1, std::numeric_limits<int>::min()}) {
            Bytes bytes;
            append_varint(bytes, value);
            auto decoded = decode_varint(bytes);
            require(decoded && decoded->value == value && decoded->size == bytes.size(), "VarInt round trip failed");
            for (std::size_t size = 0; size + 1 < bytes.size(); ++size)
                require(!decode_varint(std::span(bytes).first(size)), "Partial VarInt should wait");
        }
        require(decode_varint(Bytes{0xff, 0xff, 0xff, 0xff, 0x0f})->value == -1, "Negative VarInt fixture failed");
        rejects([] { decode_varint(Bytes{0xff, 0xff, 0xff, 0xff, 0x10}); });
        rejects([] { decode_varint(Bytes{0x80, 0x80, 0x80}, 3); });
        Bytes buffer;
        auto framed = frame(handshake());
        for (std::size_t i = 0; i < framed.size(); ++i) {
            buffer.push_back(framed[i]);
            auto packet = take_frame(buffer);
            require(packet.has_value() == (i == framed.size() - 1), "Fragment reassembly failed");
        }
        require(buffer.empty(), "Frame buffer not consumed");
        for (Bytes bad : {Bytes{0}, Bytes{0x81, 0x20}, Bytes{0x80, 0x80, 0x80}})
            rejects([&] { (void)take_frame(bad); });
        Bytes concatenated{1, 0, 1, 0};
        require(take_frame(concatenated) == Bytes{0}, "First coalesced frame failed");
        require(take_frame(concatenated) == Bytes{0}, "Second coalesced frame failed");
        require(!take_frame(concatenated), "Unexpected trailing frame");
        require(json_quote("a\"\\\n\t") == "\"a\\\"\\\\\\u000a\\u0009\"", "JSON escaping failed");
        rejects([] { (void)json_quote(std::string("\xc0\xaf", 2)); });
        rejects([] { (void)json_quote(std::string("\xed\xa0\x80", 3)); });
        rejects([] { (void)json_quote(std::string("\xf4\x90\x80\x80", 4)); });
        Bytes unicode;
        append_string(unicode, "\xf0\x9f\x8c\x8d");
        Reader unicode_reader(unicode);
        require(unicode_reader.string(2).size() == 4, "UTF-8 supplementary character failed");
        rejects([&] { Reader reader(unicode); (void)reader.string(1); });
        Bytes truncated{5, 'a'};
        rejects([&] { Reader reader(truncated); (void)reader.string(255); });
        Status status;
        for (int version : {774, 767, -1}) {
            Session session;
            require(session.handle(handshake(1, version), status).bytes.empty(), "Handshake must not respond");
            auto reply = session.handle(Bytes{0}, status);
            auto payload = take_frame(reply.bytes);
            require(payload.has_value(), "Missing status response");
            Reader reader(*payload);
            require(reader.varint() == 0, "Status packet id mismatch");
            require(reader.string(32767).find("\"protocol\":774") != std::string::npos, "Wrong protocol advertised");
            reader.require_end();
            rejects([&] { session.handle(Bytes{0}, status); });
        }
        for (bool request_status : {false, true}) {
            Session session;
            session.handle(handshake(), status);
            if (request_status) session.handle(Bytes{0}, status);
            Bytes ping{1, 0xff, 0xfe, 0xdc, 0xba, 0x98, 0x76, 0x54, 0x32};
            auto reply = session.handle(ping, status);
            require(reply.close && take_frame(reply.bytes) == ping, "Ping must echo all 64 bits and close");
            rejects([&] { session.handle(ping, status); });
        }
        Session login;
        auto accepted = login.handle(handshake(2), status);
        require(accepted.bytes.empty() && login.wants_login(), "Login state transition failed");
        Session wrong_version;
        auto reply = wrong_version.handle(handshake(2, 767), status);
        auto login_payload = take_frame(reply.bytes);
        Reader login_reader(*login_payload);
        require(reply.close && login_reader.varint() == 0, "Login refusal missing");
        require(login_reader.string(32767).find("1.21.11") != std::string::npos, "Version refusal reason missing");
        rejects([&] { Session session; session.handle(handshake(3), status); });
        rejects([&] { Session session; auto h = handshake(); h.push_back(0); session.handle(h, status); });
        rejects([&] { Session session; session.handle(Bytes{1}, status); });
        rejects([&] { Session session; session.handle(handshake(), status); session.handle(Bytes{1, 0}, status); });
        rejects([&] { Session session; session.handle(handshake(), status); session.handle(Bytes{0, 0}, status); });
        std::cout << checks << " protocol checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Protocol test failed: " << error.what() << '\n';
        return 1;
    }
}
