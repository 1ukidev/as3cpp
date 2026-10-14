#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace as3lsp::abc {

// Decompresses a raw DEFLATE stream (RFC 1951).
[[nodiscard]] std::optional<std::vector<std::uint8_t>> inflate(std::span<const std::uint8_t> input,
                                                               std::size_t sizeHint = 0);

// Decompresses a zlib stream (RFC 1950). The checksum is not verified.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> zlibDecompress(std::span<const std::uint8_t> input,
                                                                      std::size_t sizeHint = 0);

} // namespace as3lsp::abc
