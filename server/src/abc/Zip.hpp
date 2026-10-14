#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace as3lsp::abc {

struct ZipEntry {
    std::string name;
    std::uint16_t method = 0;
    std::uint32_t compressedSize = 0;
    std::uint32_t uncompressedSize = 0;
    std::uint32_t localHeaderOffset = 0;
};

// Lists the entries of a ZIP archive, such as a SWC file. Returns nullopt for a malformed archive.
[[nodiscard]] std::optional<std::vector<ZipEntry>> listZipEntries(std::span<const std::uint8_t> archive);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> extractZipEntry(std::span<const std::uint8_t> archive,
                                                                       const ZipEntry& entry);

} // namespace as3lsp::abc
