#include "cudacraft/crypto.hpp"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cstring>

namespace cudacraft::crypto {
namespace {
void check(NTSTATUS status) { if (status < 0) throw protocol::Error("Cryptographic operation failed"); }
struct Algorithm {
    BCRYPT_ALG_HANDLE handle = nullptr;
    explicit Algorithm(LPCWSTR name,ULONG flags=0) { check(BCryptOpenAlgorithmProvider(&handle, name, nullptr, flags)); }
    ~Algorithm() { if (handle) BCryptCloseAlgorithmProvider(handle, 0); }
};
Bytes der(std::uint8_t tag, const Bytes& content) {
    Bytes out{tag};
    if (content.size() < 128) out.push_back(static_cast<std::uint8_t>(content.size()));
    else if (content.size() < 256) { out.push_back(0x81); out.push_back(static_cast<std::uint8_t>(content.size())); }
    else { out.push_back(0x82); out.push_back(static_cast<std::uint8_t>(content.size() >> 8)); out.push_back(static_cast<std::uint8_t>(content.size())); }
    out.insert(out.end(), content.begin(), content.end());
    return out;
}
void append(Bytes& out, const Bytes& data) { out.insert(out.end(), data.begin(), data.end()); }
Bytes integer(std::span<const std::uint8_t> bytes) {
    Bytes value;
    if (bytes.front() & 0x80) value.push_back(0);
    value.insert(value.end(), bytes.begin(), bytes.end());
    return der(2, value);
}
}
Bytes random_bytes(std::size_t count) {
    if (count > 65536) throw protocol::Error("Random request too large");
    Bytes bytes(count);
    check(BCryptGenRandom(nullptr, bytes.data(), static_cast<ULONG>(bytes.size()), BCRYPT_USE_SYSTEM_PREFERRED_RNG));
    return bytes;
}
Bytes sha1(std::span<const std::uint8_t> bytes) {
    Algorithm algorithm(BCRYPT_SHA1_ALGORITHM);
    Bytes digest(20);
    check(BCryptHash(algorithm.handle, nullptr, 0, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), digest.data(), static_cast<ULONG>(digest.size())));
    return digest;
}
Bytes hmac_sha256(std::span<const std::uint8_t> key,std::span<const std::uint8_t> message) {
    if(key.size()!=32||message.size()!=32)throw protocol::Error("Invalid local challenge size");
    Algorithm algorithm(BCRYPT_SHA256_ALGORITHM,BCRYPT_ALG_HANDLE_HMAC_FLAG);Bytes result(32);
    check(BCryptHash(algorithm.handle,const_cast<PUCHAR>(key.data()),32,const_cast<PUCHAR>(message.data()),32,result.data(),32));return result;
}
std::string signed_hex(std::span<const std::uint8_t> digest) {
    if (digest.empty()) throw protocol::Error("Empty digest");
    Bytes magnitude(digest.begin(), digest.end());
    const bool negative = (magnitude[0] & 0x80) != 0;
    if (negative) {
        unsigned carry = 1;
        for (auto i = magnitude.rbegin(); i != magnitude.rend(); ++i) {
            const unsigned value = static_cast<unsigned>(static_cast<std::uint8_t>(~*i)) + carry;
            *i = static_cast<std::uint8_t>(value);
            carry = value >> 8;
        }
    }
    constexpr char hex[] = "0123456789abcdef";
    std::string text;
    for (auto byte : magnitude) { text += hex[byte >> 4]; text += hex[byte & 15]; }
    const auto start = text.find_first_not_of('0');
    if (start == std::string::npos) return "0";
    return (negative ? "-" : "") + text.substr(start);
}
bool equal(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) {
    if (a.size() != b.size()) return false;
    volatile unsigned difference = 0;
    for (std::size_t i = 0; i < a.size(); ++i) difference = difference | (a[i] ^ b[i]);
    return difference == 0;
}
struct ServerKey::Impl {
    Algorithm algorithm{BCRYPT_RSA_ALGORITHM};
    BCRYPT_KEY_HANDLE key = nullptr;
    Bytes public_key;
    ~Impl() { if (key) BCryptDestroyKey(key); }
};
ServerKey::ServerKey() : impl_(std::make_unique<Impl>()) {
    check(BCryptGenerateKeyPair(impl_->algorithm.handle, &impl_->key, 1024, 0));
    check(BCryptFinalizeKeyPair(impl_->key, 0));
    ULONG size = 0;
    check(BCryptExportKey(impl_->key, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &size, 0));
    Bytes blob(size);
    check(BCryptExportKey(impl_->key, nullptr, BCRYPT_RSAPUBLIC_BLOB, blob.data(), size, &size, 0));
    BCRYPT_RSAKEY_BLOB header{};
    std::memcpy(&header, blob.data(), sizeof(header));
    auto fields = std::span(blob).subspan(sizeof(header));
    Bytes rsa = integer(fields.subspan(header.cbPublicExp, header.cbModulus));
    append(rsa, integer(fields.first(header.cbPublicExp)));
    Bytes bits{0};
    append(bits, der(0x30, rsa));
    // SubjectPublicKeyInfo: rsaEncryption OID + NULL, followed by the RSA key bit string.
    Bytes spki{0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00};
    append(spki, der(3, bits));
    impl_->public_key = der(0x30, spki);
}
ServerKey::~ServerKey() = default;
const Bytes& ServerKey::public_der() const { return impl_->public_key; }
Bytes ServerKey::decrypt(std::span<const std::uint8_t> ciphertext) const {
    if (ciphertext.size() != 128) throw protocol::Error("Invalid RSA ciphertext size");
    Bytes plaintext(128);
    ULONG size = 0;
    check(BCryptDecrypt(impl_->key, const_cast<PUCHAR>(ciphertext.data()), static_cast<ULONG>(ciphertext.size()), nullptr, nullptr, 0, plaintext.data(), static_cast<ULONG>(plaintext.size()), &size, BCRYPT_PAD_PKCS1));
    plaintext.resize(size);
    return plaintext;
}
struct Cipher::Impl {
    Algorithm algorithm{BCRYPT_AES_ALGORITHM};
    BCRYPT_KEY_HANDLE key = nullptr;
    std::array<std::uint8_t, 16> iv{};
    bool encrypt;
    explicit Impl(bool encrypting) : encrypt(encrypting) {}
    ~Impl() { if (key) BCryptDestroyKey(key); SecureZeroMemory(iv.data(), iv.size()); }
};
Cipher::Cipher(std::span<const std::uint8_t> secret, bool encrypt) : impl_(std::make_unique<Impl>(encrypt)) {
    if (secret.size() != 16) throw protocol::Error("AES secret must be 16 bytes");
    check(BCryptSetProperty(impl_->algorithm.handle, BCRYPT_CHAINING_MODE, reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(BCRYPT_CHAIN_MODE_CFB)), sizeof(BCRYPT_CHAIN_MODE_CFB), 0));
    check(BCryptGenerateSymmetricKey(impl_->algorithm.handle, &impl_->key, nullptr, 0, const_cast<PUCHAR>(secret.data()), 16, 0));
    ULONG block_size = 1;
    check(BCryptSetProperty(impl_->key, BCRYPT_MESSAGE_BLOCK_LENGTH, reinterpret_cast<PUCHAR>(&block_size), sizeof(block_size), 0));
    std::copy(secret.begin(), secret.end(), impl_->iv.begin());
}
Cipher::~Cipher() = default;
Bytes Cipher::transform(std::span<const std::uint8_t> bytes) {
    if (bytes.empty()) return {};
    if (bytes.size() > 131072) throw protocol::Error("Cipher input exceeds connection budget");
    Bytes result(bytes.size());
    ULONG size = 0;
    auto operation = impl_->encrypt ? BCryptEncrypt : BCryptDecrypt;
    check(operation(impl_->key, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()), nullptr, impl_->iv.data(), 16, result.data(), static_cast<ULONG>(result.size()), &size, 0));
    if (size != bytes.size()) throw protocol::Error("Unexpected AES output size");
    return result;
}
}
