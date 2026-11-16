#include "cudacraft/auth.hpp"
#include "cudacraft/crypto.hpp"
#include "cudacraft/compression.hpp"
#include <iostream>

using namespace cudacraft;
using protocol::Bytes;
namespace {
int checks = 0;
void require(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
template<class F> void rejects(F fn) {
    bool rejected = false;
    try { fn(); } catch (const protocol::Error&) { rejected = true; }
    require(rejected, "Expected rejection");
}
}
int main() {
    try {
        for (int threshold : {0, 1, 256, 65536}) {
            for (std::size_t size : {1u, 255u, 256u, 4096u, 65536u}) {
                Bytes payload(size);
                for (std::size_t i = 0; i < size; ++i) payload[i] = static_cast<std::uint8_t>(i * 37u);
                auto encoded = protocol::compress_packet(payload, threshold);
                require(protocol::decompress_packet(encoded, threshold) == payload, "Compression round trip failed");
                if (size >= static_cast<std::size_t>(threshold)) {
                    encoded.push_back(0);
                    rejects([&] { protocol::decompress_packet(encoded, threshold); });
                }
            }
        }
        rejects([] { protocol::decompress_packet(Bytes{0, 1}, 0); });
        rejects([] { protocol::decompress_packet(Bytes{1, 1}, 256); });
        rejects([] { protocol::decompress_packet(Bytes{0x80, 0x80, 4}, 256); });
        rejects([] { protocol::decompress_packet(Bytes{0}, 256); });
        auto corrupt = protocol::compress_packet(Bytes(500, 42), 256);
        corrupt.back() ^= 0xff;
        rejects([&] { protocol::decompress_packet(corrupt, 256); });
        require(crypto::signed_hex(Bytes{0}) == "0", "Zero signed hash");
        require(crypto::signed_hex(Bytes{0, 15}) == "f", "Positive signed hash");
        require(crypto::signed_hex(Bytes{255}) == "-1", "Negative signed hash");
        require(crypto::signed_hex(Bytes{128, 0}) == "-8000", "Minimum signed hash");
        const Bytes abc{'a', 'b', 'c'};
        require(crypto::sha1(abc) == Bytes{0xa9,0x99,0x3e,0x36,0x47,0x06,0x81,0x6a,0xba,0x3e,0x25,0x71,0x78,0x50,0xc2,0x6c,0x9c,0xd0,0xd8,0x9d}, "SHA1 known vector failed");
        const Bytes key{0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15};
        crypto::Cipher encrypt(key, true), decrypt(key, false);
        Bytes plain(4097);
        for (std::size_t i = 0; i < plain.size(); ++i) plain[i] = static_cast<std::uint8_t>(i);
        auto encrypted = encrypt.transform(plain);
        Bytes recovered;
        for (auto byte : encrypted) {
            auto piece = decrypt.transform(Bytes{byte});
            recovered.insert(recovered.end(), piece.begin(), piece.end());
        }
        require(recovered == plain, "Continuous CFB8 fragmented stream failed");
        rejects([] { crypto::Cipher cipher(Bytes(15), true); });
        require(auth::valid_username("Cuda_Tester123"), "Valid username rejected");
        for (auto name : {"", "contains space", "../bad", "12345678901234567"}) require(!auth::valid_username(name), "Invalid username accepted");
        const std::string profile_json = R"({"id":"000102030405060708090a0b0c0d0e0f","name":"CudaTester","properties":[{"name":"textures","value":"abc","signature":"sig"}]})";
        const auto profile = auth::parse_profile(profile_json, "CudaTester");
        require(profile.uuid[15] == 15 && profile.properties[0].signature == "sig", "Profile decoding failed");
        rejects([&] { auth::parse_profile(profile_json, "WrongName"); });
        rejects([] { auth::parse_profile("{", "CudaTester"); });
        rejects([] { auth::parse_profile(R"({"id":"bad","name":"CudaTester"})", "CudaTester"); });
        rejects([] { auth::parse_profile(std::string(100, '['), "CudaTester"); });
        auth::Pool pool([&](const std::string& name, const std::string&) {
            if (name == "RejectMe") throw protocol::Error("Rejected");
            return auth::parse_profile(profile_json, name);
        });
        require(pool.submit("CudaTester", "abc").get().name == "CudaTester", "Async verifier failed");
        rejects([&] { pool.submit("RejectMe", "abc").get(); });
        std::cout << checks << " crypto/auth/compression checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
