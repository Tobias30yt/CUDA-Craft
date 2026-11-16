#include "cudacraft/login.hpp"
#include "cudacraft/compression.hpp"
#include "cudacraft/generated_game_data.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <utility>

namespace cudacraft {
using namespace protocol;
namespace {
void append(Bytes& out, std::span<const std::uint8_t> data) { out.insert(out.end(), data.begin(), data.end()); }
void byte_array(Bytes& out, std::span<const std::uint8_t> bytes) { append_varint(out, static_cast<std::int32_t>(bytes.size())); append(out, bytes); }
Bytes read_byte_array(Reader& reader, std::size_t limit) {
    const auto size = reader.varint();
    if (size < 0 || static_cast<std::size_t>(size) > limit) throw Error("Invalid byte array length");
    auto bytes = reader.bytes(static_cast<std::size_t>(size));
    return Bytes(bytes.begin(), bytes.end());
}
}
LoginSession::LoginSession(crypto::ServerKey& key, auth::Pool& auth, World& world, bool authenticate,const auth::LocalSession* local)
    : key_(key), auth_(auth), world_(world), authenticate_(authenticate),local_(local) {}
void LoginSession::send(std::span<const std::uint8_t> payload) {
    if (payload.empty() || payload.size() > 0x1ffff0) throw Error("Response too large");
    auto framed = compression_ < 0 ? frame(payload) : frame(compress_packet(payload, compression_));
    if (encrypt_) framed = encrypt_->transform(framed);
    if (output_.size() + framed.size() > 131072) throw Error("Login output budget exceeded");
    append(output_, framed);
}
void LoginSession::disconnect(std::string_view reason) {
    Bytes packet{0};
    append_string(packet, "{\"text\":" + json_quote(reason) + "}");
    send(packet);
    state_ = State::closed;
}
void LoginSession::feed(std::span<const std::uint8_t> wire) {
    if (state_ == State::closed) return;
    if (input_.size() + wire.size() > 131072) throw Error("Login input budget exceeded");
    if (decrypt_) append(input_, decrypt_->transform(wire));
    else append(input_, wire);
    while (state_ != State::closed) {
        auto packet = take_frame(input_, max_login_payload + 128);
        if (!packet) break;
        const bool encrypted_before = decrypt_ != nullptr;
        if (compression_ >= 0) *packet = decompress_packet(*packet, compression_);
        process(*packet);
        // A single TCP read may contain the plaintext encryption response and encrypted next bytes.
        if (!encrypted_before && decrypt_ && !input_.empty()) input_ = decrypt_->transform(input_);
    }
}
void LoginSession::process(std::span<const std::uint8_t> packet) {
    if(state_==State::play) { game_->handle(packet); return; }
    Reader reader(packet);
    const auto id = reader.varint();
    if (state_ == State::start) {
        if (id != 0) throw Error("Expected Login Start");
        name_ = reader.string(16);
        if (!auth::valid_username(name_)) { disconnect("Invalid player name."); return; }
        auto uuid = reader.bytes(16);
        std::copy(uuid.begin(), uuid.end(), requested_uuid_.begin());
        reader.require_end();
        if(local_) {
            if(local_->owner.name!=name_||!crypto::equal(local_->owner.uuid,requested_uuid_)){disconnect("This private world belongs to another local session.");return;}
            local_nonce_=crypto::random_bytes(32);
            send(Packet(4).var(1).str("cudacraft:local_session").raw(local_nonce_).data);state_=State::local_challenge;
        } else request_encryption();
    } else if(state_==State::local_challenge) {
        if(id!=2||reader.varint()!=1||reader.bytes(1)[0]!=1){disconnect("CUDA Craft local-session handshake required.");return;}
        const auto proof=reader.bytes(32);reader.require_end();
        if(!crypto::equal(proof,crypto::hmac_sha256(local_->secret,local_nonce_))){disconnect("Invalid private-world session proof.");return;}
        std::fill(local_nonce_.begin(),local_nonce_.end(),0);request_encryption();
    } else if (state_ == State::encryption) {
        if (id != 1) throw Error("Expected Encryption Response");
        const auto encrypted_secret = read_byte_array(reader, 128);
        const auto encrypted_token = read_byte_array(reader, 128);
        reader.require_end();
        auto secret = key_.decrypt(encrypted_secret);
        const auto token = key_.decrypt(encrypted_token);
        if (secret.size() != 16 || !crypto::equal(token, token_)) throw Error("Invalid encryption response");
        decrypt_ = std::make_unique<crypto::Cipher>(secret, false);
        encrypt_ = std::make_unique<crypto::Cipher>(secret, true);
        Bytes digest_input = secret;
        append(digest_input, key_.public_der());
        const auto hash = crypto::signed_hex(crypto::sha1(digest_input));
        std::fill(secret.begin(), secret.end(), 0);
        std::fill(digest_input.begin(), digest_input.end(), 0);
        if(local_){std::promise<auth::Profile> result;verification_=result.get_future();result.set_value(local_->owner);}
        else verification_ = auth_.submit(name_, hash);
        state_ = State::verifying;
    } else if (state_ == State::acknowledgement) {
        if (id != 3) throw Error("Expected Login Acknowledged");
        reader.require_end();
        Packet brand(1); brand.str("minecraft:brand").str("CUDA Craft"); send(brand.data);
        send(Packet(0x0c).var(1).str("minecraft:vanilla").data);
        send(Packet(0x0e).var(0).data);
        state_=State::known_packs;
    } else if(state_==State::known_packs || state_==State::configuration) {
        if(id==0 || id==2) return;
        if(id==7 && state_==State::known_packs) {
            if(reader.varint()!=0) throw Error("Unexpected known packs response");
            reader.require_end();
            for(const auto& registry:game_data::configuration_packets()) send(registry);
            send(Packet(3).data);
            state_=State::configuration;
        } else if(id==3 && state_==State::configuration) {
            reader.require_end();
            game_=std::make_unique<GameSession>(world_,profile_,[this](std::span<const std::uint8_t> bytes){send(bytes);});
            state_=State::play;
            game_->start();
        } else throw Error("Unexpected configuration packet");
    } else throw Error("Unexpected packet while verifying login");
}
void LoginSession::request_encryption() {
    token_=crypto::random_bytes(16);Bytes response{1};append_string(response,"");byte_array(response,key_.public_der());byte_array(response,token_);
    response.push_back(local_?0:authenticate_?1:0);send(response);state_=State::encryption;
}
void LoginSession::poll() {
    if(state_==State::play) {
        if(game_->closing()){state_=State::closed;return;}
        game_->poll();
        if(game_->expired()) { send(Packet(0x20).text("Connection timed out or could not keep up.").data); state_=State::closed; }
        return;
    }
    if (state_ != State::verifying || verification_.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    try {
        const auto profile = verification_.get();
        if (profile.name != name_ || !crypto::equal(profile.uuid, requested_uuid_)) throw Error("Verified identity does not match Login Start");
        Bytes compression{3};
        append_varint(compression, 256);
        send(compression);
        compression_ = 256;
        Bytes success{2};
        append(success, profile.uuid);
        append_string(success, profile.name);
        append_varint(success, static_cast<std::int32_t>(profile.properties.size()));
        for (const auto& property : profile.properties) {
            append_string(success, property.name);
            append_string(success, property.value);
            success.push_back(property.signature.has_value() ? 1 : 0);
            if (property.signature) append_string(success, *property.signature);
        }
        send(success);
        profile_=profile;
        state_ = State::acknowledgement;
    } catch (const std::exception& error) {
        // Controlled messages only: never log secrets, session hashes or profile properties.
        std::cerr << "Login verification failed: " << error.what() << '\n';
        disconnect("Could not verify your Minecraft session. Please sign in again and retry.");
    }
}
Bytes LoginSession::take_output() { return std::exchange(output_, {}); }
}
