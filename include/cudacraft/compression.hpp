#pragma once
#include "cudacraft/protocol.hpp"
namespace cudacraft::protocol {
// Payloads only; the outer packet length is handled by frame()/take_frame().
Bytes compress_packet(std::span<const std::uint8_t> payload, int threshold);
Bytes decompress_packet(std::span<const std::uint8_t> body, int threshold);
}
