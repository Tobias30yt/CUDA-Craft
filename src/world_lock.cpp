#include "cudacraft/world_lock.hpp"
#include <stdexcept>
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace cudacraft {
WorldLock::WorldLock(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    const auto file = directory / "native-world.lock";
    handle_ = CreateFileW(file.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle_ == INVALID_HANDLE_VALUE) {
        const auto error = GetLastError();
        handle_ = nullptr;
        if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)
            throw std::runtime_error("World is already open in another server. Close it before opening this save.");
        throw std::runtime_error("Cannot lock world directory (Windows error " + std::to_string(error) + ")");
    }
}
WorldLock::~WorldLock() { if (handle_) CloseHandle(handle_); }
}
