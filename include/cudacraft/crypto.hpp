#pragma once
#include "cudacraft/protocol.hpp"
#include <memory>

namespace cudacraft::crypto {
using protocol::Bytes;
Bytes random_bytes(std::size_t count);
Bytes sha1(std::span<const std::uint8_t> bytes);
Bytes hmac_sha256(std::span<const std::uint8_t> key,std::span<const std::uint8_t> message);
std::string signed_hex(std::span<const std::uint8_t> digest);
bool equal(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b);
class ServerKey {
public:
    ServerKey();
    ~ServerKey();
    ServerKey(const ServerKey&) = delete;
    ServerKey& operator=(const ServerKey&) = delete;
    const Bytes& public_der() const;
    Bytes decrypt(std::span<const std::uint8_t> ciphertext) const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
class Cipher {
public:
    Cipher(std::span<const std::uint8_t> secret, bool encrypt);
    ~Cipher();
    Cipher(const Cipher&) = delete;
    Cipher& operator=(const Cipher&) = delete;
    Bytes transform(std::span<const std::uint8_t> bytes);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
