#include "cudacraft/auth.hpp"
#include <nlohmann/json.hpp>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>

namespace cudacraft::auth {
bool valid_username(std::string_view name) {
    if (name.empty() || name.size() > 16) return false;
    for (char c : name) if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_')) return false;
    return true;
}
Profile parse_profile(std::string_view text, std::string_view requested_name) {
    if (text.size() > 65536 || !valid_username(requested_name)) throw protocol::Error("Invalid authentication response");
    // Authentication JSON has a tiny fixed schema. Bound nesting before recursive DOM parsing.
    int depth = 0;
    bool quoted = false, escaped = false;
    for (char c : text) {
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
        } else if (c == '"') quoted = true;
        else if (c == '{' || c == '[') { if (++depth > 8) throw protocol::Error("Authentication JSON too deep"); }
        else if (c == '}' || c == ']') --depth;
    }
    try {
        const auto data = nlohmann::json::parse(text);
        Profile profile;
        profile.name = data.at("name").get<std::string>();
        if (profile.name != requested_name) throw protocol::Error("Authenticated username mismatch");
        const auto id = data.at("id").get<std::string>();
        if (id.size() != 32) throw protocol::Error("Invalid authenticated UUID");
        auto nibble = [](char c) -> unsigned {
            if (c >= '0' && c <= '9') return static_cast<unsigned>(c - '0');
            if (c >= 'a' && c <= 'f') return static_cast<unsigned>(c - 'a' + 10);
            if (c >= 'A' && c <= 'F') return static_cast<unsigned>(c - 'A' + 10);
            throw protocol::Error("Invalid UUID digit");
        };
        for (std::size_t i = 0; i < 16; ++i) profile.uuid[i] = static_cast<std::uint8_t>((nibble(id[2 * i]) << 4) | nibble(id[2 * i + 1]));
        if (data.contains("properties")) {
            const auto& properties = data.at("properties");
            if (!properties.is_array() || properties.size() > 16) throw protocol::Error("Invalid profile properties");
            for (const auto& entry : properties) {
                Property property{entry.at("name").get<std::string>(), entry.at("value").get<std::string>(), std::nullopt};
                if (entry.contains("signature")) property.signature = entry.at("signature").get<std::string>();
                if (property.name.size() > 64 || property.value.size() > 8192 || (property.signature && property.signature->size() > 8192)) throw protocol::Error("Profile property too large");
                profile.properties.push_back(std::move(property));
            }
        }
        return profile;
    } catch (const nlohmann::json::exception&) { throw protocol::Error("Malformed authentication JSON"); }
}
struct Pool::Impl {
    struct Job { std::string name, hash; std::promise<Profile> promise; };
    Verifier verifier;
    std::mutex mutex;
    std::condition_variable ready;
    std::deque<Job> queue;
    bool stopping = false;
    std::vector<std::jthread> workers;
    explicit Impl(Verifier function) : verifier(std::move(function)) {}
    void work() {
        for (;;) {
            Job job;
            {
                std::unique_lock lock(mutex);
                ready.wait(lock, [&] { return stopping || !queue.empty(); });
                if (stopping) return;
                job = std::move(queue.front()); queue.pop_front();
            }
            try { job.promise.set_value(verifier(job.name, job.hash)); }
            catch (...) { job.promise.set_exception(std::current_exception()); }
        }
    }
};
Pool::Pool(Verifier verifier) : impl_(std::make_unique<Impl>(std::move(verifier))) {
    try {
        for (int i = 0; i < 2; ++i) impl_->workers.emplace_back([this] { impl_->work(); });
    } catch (...) {
        { std::lock_guard lock(impl_->mutex); impl_->stopping = true; }
        impl_->ready.notify_all();
        throw;
    }
}
Pool::~Pool() {
    { std::lock_guard lock(impl_->mutex); impl_->stopping = true; impl_->queue.clear(); }
    impl_->ready.notify_all();
    // Impl declares workers last: jthreads join before mutex/verifier are destroyed.
}
std::future<Profile> Pool::submit(std::string name, std::string hash) {
    std::lock_guard lock(impl_->mutex);
    if (impl_->stopping || impl_->queue.size() >= 32) throw protocol::Error("Authentication queue full");
    Impl::Job job{std::move(name), std::move(hash), {}};
    auto future = job.promise.get_future();
    impl_->queue.push_back(std::move(job));
    impl_->ready.notify_one();
    return future;
}
}
