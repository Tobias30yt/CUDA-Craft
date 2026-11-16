#include "cudacraft/server.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <iostream>
#include <memory>
#include <utility>
#include <thread>
#ifdef CUDACRAFT_LOGIN
#include "cudacraft/login.hpp"
#include "cudacraft/gpu.hpp"
#include "cudacraft/admin.hpp"
#include "cudacraft/console.hpp"
#endif

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace cudacraft {
namespace {
using Clock = std::chrono::steady_clock;
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
int socket_error() { return WSAGetLastError(); }
bool would_block(int error) { return error == WSAEWOULDBLOCK; }
bool interrupted(int error) { return error == WSAEINTR; }
void close_socket(Socket socket) { closesocket(socket); }
struct NetworkRuntime {
    NetworkRuntime() { WSADATA data{}; if (WSAStartup(MAKEWORD(2, 2), &data)) throw std::runtime_error("WSAStartup failed"); }
    ~NetworkRuntime() { WSACleanup(); }
};
void nonblocking(Socket socket) {
    u_long yes = 1;
    if (ioctlsocket(socket, FIONBIO, &yes)) throw std::runtime_error("Cannot set nonblocking socket");
}
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
int socket_error() { return errno; }
bool would_block(int error) { return error == EWOULDBLOCK || error == EAGAIN; }
bool interrupted(int error) { return error == EINTR; }
void close_socket(Socket socket) { ::close(socket); }
struct NetworkRuntime {};
void nonblocking(Socket socket) {
    const int flags = fcntl(socket, F_GETFL, 0);
    if (flags == -1 || fcntl(socket, F_SETFL, flags | O_NONBLOCK) == -1) throw std::runtime_error("Cannot set nonblocking socket");
}
#endif
class OwnedSocket {
public:
    explicit OwnedSocket(Socket value) : value_(value) {}
    ~OwnedSocket() { if (value_ != invalid_socket) close_socket(value_); }
    OwnedSocket(const OwnedSocket&) = delete;
    OwnedSocket& operator=(const OwnedSocket&) = delete;
    Socket get() const { return value_; }
private:
    Socket value_;
};
struct Client {
    explicit Client(Socket value) : socket(value), started(Clock::now()) {}
    OwnedSocket socket;
    protocol::Session session;
#ifdef CUDACRAFT_LOGIN
    std::unique_ptr<LoginSession> login;
#endif
    protocol::Bytes input, output;
    std::size_t sent = 0;
    Clock::time_point started;
    bool closing = false;
    bool dead = false;
    bool write_shutdown = false;
    Clock::time_point close_started{};
};
constexpr std::size_t buffer_limit = 16384;

#ifdef CUDACRAFT_LOGIN
void collect_login(Client& client) {
    auto output = client.login->take_output();
    if (client.output.size() + output.size() > 131072) throw protocol::Error("Connection output budget exceeded");
    client.output.insert(client.output.end(), output.begin(), output.end());
    client.closing = client.login->closing();
}
#endif

void receive(Client& client, const protocol::Status& status
#ifdef CUDACRAFT_LOGIN
    , crypto::ServerKey& key, auth::Pool& authentication, World& world, bool authenticate_clients,const auth::LocalSession* local
#endif
) {
    std::array<std::uint8_t, 4096> data{};
    const auto count = recv(client.socket.get(), reinterpret_cast<char*>(data.data()), static_cast<int>(data.size()), 0);
    if (count == 0) { if(client.closing)client.dead=true;else client.closing=true; return; }
    if (count < 0) {
        const auto error = socket_error();
        if (!would_block(error) && !interrupted(error)) client.dead = true;
        return;
    }
    // Drain peer data during graceful close; unread TCP bytes can turn close into
    // an RST on Windows and discard the already queued disconnect reason.
    if(client.closing)return;
    try {
#ifdef CUDACRAFT_LOGIN
        world.performance.bytes_received+=static_cast<std::uint64_t>(count);
        if (client.login) {
            client.login->feed(std::span(data).first(static_cast<std::size_t>(count)));
            collect_login(client);
            return;
        }
#endif
        client.input.insert(client.input.end(), data.begin(), data.begin() + count);
        if (client.input.size() > buffer_limit) { client.dead = true; return; }
        while (!client.closing) {
            auto payload = protocol::take_frame(client.input);
            if (!payload) break;
            auto reply = client.session.handle(*payload, status);
            if (client.output.size() + reply.bytes.size() > buffer_limit) throw protocol::Error("Output limit exceeded");
            client.output.insert(client.output.end(), reply.bytes.begin(), reply.bytes.end());
            client.closing = reply.close;
            if (client.session.wants_login()) {
#ifdef CUDACRAFT_LOGIN
                client.login = std::make_unique<LoginSession>(key, authentication, world, authenticate_clients,local);
                client.login->feed(client.input);
                client.input.clear();
                collect_login(client);
#else
                protocol::Bytes refusal{0};
                protocol::append_string(refusal, "{\"text\":\"Online login currently requires the Windows build.\"}");
                auto framed = protocol::frame(refusal);
                client.output.insert(client.output.end(), framed.begin(), framed.end());
                client.closing = true;
#endif
                break;
            }
        }
    } catch (const protocol::Error& error) {
        std::cerr<<"Connection rejected: "<<error.what()<<'\n'; client.dead = true;
    }
}
std::size_t transmit(Client& client) {
    const auto remaining = client.output.size() - client.sent;
    if (!remaining) return 0;
    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif
    const auto count = send(client.socket.get(), reinterpret_cast<const char*>(client.output.data() + client.sent), static_cast<int>(remaining), flags);
    if (count > 0) {
        client.sent += static_cast<std::size_t>(count);
        if (client.sent == client.output.size()) { client.output.clear(); client.sent = 0; }
    } else if (count == 0) client.dead = true;
    else {
        const auto error = socket_error();
        if (!would_block(error) && !interrupted(error)) client.dead = true;
    }
    return count>0?static_cast<std::size_t>(count):0;
}
}

void run_server(const ServerConfig& config, const volatile std::sig_atomic_t& stop) {
    [[maybe_unused]] NetworkRuntime runtime;
    if (config.max_connections == 0 || config.max_connections > FD_SETSIZE - 1 || config.timeout_ms < 1 || config.run_for_ms < 0) throw std::runtime_error("Invalid server limits");
    (void)config.status.json();
#ifdef CUDACRAFT_LOGIN
    if(config.local_session&&(config.bind_address!="127.0.0.1"||config.port!=0))throw std::runtime_error("Private local sessions require loopback and an ephemeral port");
    crypto::ServerKey key;
    auth::Pool authentication(config.verifier);
    World world(config.world_directory,config.terrain);
    ConsoleInput console;
    if(config.local_session) {
        world.local_session=true;world.admin().joined(config.local_session->owner);
        world.admin().execute("op "+config.local_session->owner.name,nullptr,[](const std::string& text){std::cout<<"[Local] "<<text<<std::endl;});
    }
    std::cout<<"Terrain: "<<((gpu::compiled() && world.options().generator!="density")?"CUDA":"CPU")<<" "<<world.options().generator<<", seed "<<world.options().seed
        <<(world.options().generator!="flat"?" (experimental terrain; not full vanilla worldgen)":"")<<std::endl;
#endif
    OwnedSocket listener(::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (listener.get() == invalid_socket) throw std::runtime_error("Cannot create listener");
#ifndef _WIN32
    if (listener.get() >= FD_SETSIZE) throw std::runtime_error("Listener exceeds select descriptor limit");
#endif
    int yes = 1;
#ifdef _WIN32
    const int reuse_option = SO_EXCLUSIVEADDRUSE;
#else
    const int reuse_option = SO_REUSEADDR;
#endif
    if (setsockopt(listener.get(), SOL_SOCKET, reuse_option, reinterpret_cast<const char*>(&yes), sizeof(yes))) throw std::runtime_error("Cannot configure listener");
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config.port);
    if (inet_pton(AF_INET, config.bind_address.c_str(), &address.sin_addr) != 1) throw std::runtime_error("--bind requires an IPv4 address");
    if (::bind(listener.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address))) throw std::runtime_error("Cannot bind listener (socket error " + std::to_string(socket_error()) + ")");
    if (listen(listener.get(), SOMAXCONN)) throw std::runtime_error("Cannot listen");
    nonblocking(listener.get());
#ifdef _WIN32
    int address_size = sizeof(address);
#else
    socklen_t address_size = sizeof(address);
#endif
    if (getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &address_size)) throw std::runtime_error("Cannot obtain listener port");
    std::cout << "LISTENING " << config.bind_address << ':' << ntohs(address.sin_port) << " | CUDA Craft | Minecraft 1.21.11 (774) | default mode: " << (config.terrain.gamemode==0?"survival":"creative") << std::endl;
    const auto started = Clock::now();
    std::vector<std::unique_ptr<Client>> clients;
#ifdef CUDACRAFT_LOGIN
    std::cout<<"Console ready: help | op <player> | perf | monitor on | save-all | stop"<<std::endl;
#endif
    while (!stop) {
        const auto now = Clock::now();
#ifdef CUDACRAFT_LOGIN
        for(const auto& command:console.poll())world.admin().execute(command,nullptr,[](const std::string& text){std::cout<<"[Console] "<<text<<std::endl;});
        if(config.local_session&&console.ended()){std::cout<<"Local client control pipe closed; saving world."<<std::endl;break;}
        if(world.admin().stopping)break;
        world.tick();
        world.admin().poll();
#endif
        if (config.run_for_ms && now - started >= std::chrono::milliseconds(config.run_for_ms)) break;
        fd_set reads, writes;
        FD_ZERO(&reads); FD_ZERO(&writes);
        FD_SET(listener.get(), &reads);
        Socket largest = listener.get();
        for (auto& client : clients) {
            FD_SET(client->socket.get(), &reads);
            if (!client->output.empty()) FD_SET(client->socket.get(), &writes);
            largest = std::max(largest, client->socket.get());
        }
        timeval timeout{0, 50000};
#ifdef CUDACRAFT_LOGIN
        timeout.tv_usec=world.tick_wait_microseconds();
#endif
        const auto wait_start=Clock::now();
#ifdef _WIN32
        const auto ready = select(0, &reads, &writes, nullptr, &timeout);
#else
        const auto ready = select(largest + 1, &reads, &writes, nullptr, &timeout);
#endif
        const auto waited=Clock::now()-wait_start;
        if (ready < 0) {
            if (interrupted(socket_error())) continue;
            throw std::runtime_error("select failed");
        }
        for (auto& client : clients) {
            int deadline_ms = config.timeout_ms;
#ifdef CUDACRAFT_LOGIN
            if (client->login) deadline_ms = 30000;
            const bool in_play=client->login && client->login->playing();
#else
            const bool in_play=false;
#endif
            if (!in_play && !client->closing && Clock::now() - client->started >= std::chrono::milliseconds(deadline_ms)) { client->dead = true; continue; }
            auto advertised_status=config.status;
#ifdef CUDACRAFT_LOGIN
            advertised_status.online_players=static_cast<std::int32_t>(world.players.size());
#endif
            if (FD_ISSET(client->socket.get(), &reads)) receive(*client, advertised_status
#ifdef CUDACRAFT_LOGIN
                , key, authentication, world, config.authenticate_clients,config.local_session?&*config.local_session:nullptr
#endif
            );
#ifdef CUDACRAFT_LOGIN
            if (client->login && !client->dead && !client->closing) {
                try { client->login->poll(); collect_login(*client); }
                catch (const protocol::Error&) { client->dead = true; }
            }
#endif
            if (!client->dead && FD_ISSET(client->socket.get(), &writes)) {
                const auto sent=transmit(*client);
#ifdef CUDACRAFT_LOGIN
                world.performance.bytes_sent+=sent;
#else
                (void)sent;
#endif
            }
            if(client->closing) {
                if(client->close_started==Clock::time_point{})client->close_started=Clock::now();
                if(client->output.empty()&&!client->write_shutdown) {
#ifdef _WIN32
                    shutdown(client->socket.get(),SD_SEND);
#else
                    shutdown(client->socket.get(),SHUT_WR);
#endif
                    client->write_shutdown=true;
                }
                if(Clock::now()-client->close_started>=std::chrono::milliseconds(250))client->dead=true;
            }
        }
        std::erase_if(clients, [](const auto& client) { return client->dead; });
        if (FD_ISSET(listener.get(), &reads)) {
            // Bound accept work per iteration so connection floods cannot starve existing clients.
            for (int accepted = 0; accepted < 16; ++accepted) {
                const auto socket = accept(listener.get(), nullptr, nullptr);
                if (socket == invalid_socket) {
                    const auto error = socket_error();
                    if (would_block(error) || interrupted(error)) break;
                    throw std::runtime_error("accept failed (socket error " + std::to_string(error) + ")");
                }
                auto client = std::make_unique<Client>(socket);
                if (clients.size() >= config.max_connections) continue;
#ifndef _WIN32
                if (socket >= FD_SETSIZE) continue;
#endif
                nonblocking(socket);
                clients.push_back(std::move(client));
            }
        }
#ifdef CUDACRAFT_LOGIN
        world.performance.connections=clients.size();world.performance.queued_bytes=0;
        for(const auto& client:clients)world.performance.queued_bytes+=client->output.size()-client->sent;
        world.performance.work(std::chrono::duration<double,std::milli>(Clock::now()-now-waited).count());
#else
        (void)waited;
#endif
    }
#ifdef CUDACRAFT_LOGIN
    for(auto* p:world.players)p->kick("Server stopping");
    for(auto& client:clients)if(client->login){try{client->login->poll();collect_login(*client);}catch(const protocol::Error&){client->dead=true;}}
    const auto drain_until=Clock::now()+std::chrono::milliseconds(200);
    while(Clock::now()<drain_until){bool pending=false;for(auto& client:clients)if(!client->dead&&!client->output.empty()){world.performance.bytes_sent+=transmit(*client);pending|=!client->output.empty();}if(!pending)break;std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    clients.clear();world.mark_dirty();world.save();
#endif
    std::cout << "CUDA Craft stopped." << std::endl;
}
}
