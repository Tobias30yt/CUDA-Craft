#pragma once
#include "cudacraft/auth.hpp"
#include "cudacraft/crypto.hpp"
#include "cudacraft/game.hpp"

namespace cudacraft {
class LoginSession {
public:
    LoginSession(crypto::ServerKey& key, auth::Pool& auth, World& world, bool authenticate = true,const auth::LocalSession* local=nullptr);
    void feed(std::span<const std::uint8_t> bytes);
    void poll();
    protocol::Bytes take_output();
    bool closing() const { return state_ == State::closed; }
    bool playing() const { return state_ == State::play; }
private:
    enum class State { start, local_challenge, encryption, verifying, acknowledgement, known_packs, configuration, play, closed };
    State state_ = State::start;
    crypto::ServerKey& key_;
    auth::Pool& auth_;
    World& world_;
    bool authenticate_;
    const auth::LocalSession* local_;
    protocol::Bytes local_nonce_;
    auth::Profile profile_;
    std::unique_ptr<GameSession> game_;
    protocol::Bytes input_, output_, token_;
    std::string name_;
    std::array<std::uint8_t, 16> requested_uuid_{};
    std::unique_ptr<crypto::Cipher> decrypt_, encrypt_;
    std::future<auth::Profile> verification_;
    int compression_ = -1;
    void process(std::span<const std::uint8_t> packet);
    void send(std::span<const std::uint8_t> packet);
    void disconnect(std::string_view reason);
    void request_encryption();
};
}
