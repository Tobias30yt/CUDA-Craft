#pragma once
#include <filesystem>

namespace cudacraft {
// The OS releases this lease even when a process is terminated. Keep the file:
// deleting it could create two independently locked files for the same world.
class WorldLock {
public:
    explicit WorldLock(const std::filesystem::path& directory);
    ~WorldLock();
    WorldLock(const WorldLock&) = delete;
    WorldLock& operator=(const WorldLock&) = delete;
private:
    void* handle_ = nullptr;
};
}
