#include "cudacraft/compression.hpp"
#include <zlib.h>
namespace cudacraft::protocol {
Bytes compress_packet(std::span<const std::uint8_t> payload, int threshold) {
    if (threshold < 0 || payload.empty() || payload.size() > 0x1ffff0) throw Error("Invalid compression input");
    Bytes body;
    if (payload.size() < static_cast<std::size_t>(threshold)) {
        body.push_back(0);
        body.insert(body.end(), payload.begin(), payload.end());
    } else {
        append_varint(body, static_cast<std::int32_t>(payload.size()));
        uLongf size = compressBound(static_cast<uLong>(payload.size()));
        const auto prefix = body.size();
        body.resize(prefix + size);
        if (compress2(body.data() + prefix, &size, payload.data(), static_cast<uLong>(payload.size()), Z_DEFAULT_COMPRESSION) != Z_OK) throw Error("Compression failed");
        body.resize(prefix + size);
    }
    return body;
}
Bytes decompress_packet(std::span<const std::uint8_t> body, int threshold) {
    Reader reader(body);
    const auto size = reader.varint();
    if (threshold < 0 || size < 0 || size > static_cast<std::int32_t>(max_login_payload)) throw Error("Invalid inflated size");
    if (size == 0) {
        if (reader.rest().empty() || reader.rest().size() >= static_cast<std::size_t>(threshold)) throw Error("Uncompressed packet exceeds threshold");
        return Bytes(reader.rest().begin(), reader.rest().end());
    }
    if (size < threshold) throw Error("Compressed packet below threshold");
    Bytes output(static_cast<std::size_t>(size));
    uLongf destination_size = static_cast<uLongf>(output.size());
    uLong source_size = static_cast<uLong>(reader.rest().size());
    const auto result = uncompress2(output.data(), &destination_size, reader.rest().data(), &source_size);
    if (result != Z_OK || destination_size != output.size() || source_size != reader.rest().size()) throw Error("Invalid compressed packet");
    return output;
}
}
