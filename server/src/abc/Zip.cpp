#include "abc/Zip.hpp"

#include "abc/Inflate.hpp"

namespace as3lsp::abc {

namespace {

constexpr std::uint32_t endOfCentralDirectorySignature = 0x06054B50;
constexpr std::uint32_t centralDirectorySignature = 0x02014B50;
constexpr std::uint32_t localHeaderSignature = 0x04034B50;
constexpr std::size_t endOfCentralDirectorySize = 22;
constexpr std::size_t centralHeaderSize = 46;
constexpr std::size_t localHeaderSize = 30;

std::uint16_t read16(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint16_t>(data[offset] | (data[offset + 1] << 8));
}

std::uint32_t read32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

} // namespace

std::optional<std::vector<ZipEntry>> listZipEntries(std::span<const std::uint8_t> archive) {
    if (archive.size() < endOfCentralDirectorySize) {
        return std::nullopt;
    }
    std::size_t end = archive.size() - endOfCentralDirectorySize;
    while (read32(archive, end) != endOfCentralDirectorySignature) {
        if (end == 0 || archive.size() - end > endOfCentralDirectorySize + 0xFFFF) {
            return std::nullopt;
        }
        --end;
    }
    const std::uint16_t count = read16(archive, end + 10);
    std::size_t offset = read32(archive, end + 16);

    std::vector<ZipEntry> entries;
    entries.reserve(count);
    for (std::uint16_t i = 0; i < count; ++i) {
        if (offset + centralHeaderSize > archive.size() ||
            read32(archive, offset) != centralDirectorySignature) {
            return std::nullopt;
        }
        ZipEntry entry;
        entry.method = read16(archive, offset + 10);
        entry.compressedSize = read32(archive, offset + 20);
        entry.uncompressedSize = read32(archive, offset + 24);
        const std::uint16_t nameLength = read16(archive, offset + 28);
        const std::uint16_t extraLength = read16(archive, offset + 30);
        const std::uint16_t commentLength = read16(archive, offset + 32);
        entry.localHeaderOffset = read32(archive, offset + 42);
        if (offset + centralHeaderSize + nameLength > archive.size()) {
            return std::nullopt;
        }
        const auto* name = reinterpret_cast<const char*>(archive.data() + offset + centralHeaderSize);
        entry.name.assign(name, nameLength);
        entries.push_back(std::move(entry));
        offset += centralHeaderSize + nameLength + extraLength + commentLength;
    }
    return entries;
}

std::optional<std::vector<std::uint8_t>> extractZipEntry(std::span<const std::uint8_t> archive,
                                                         const ZipEntry& entry) {
    const std::size_t header = entry.localHeaderOffset;
    if (header + localHeaderSize > archive.size() || read32(archive, header) != localHeaderSignature) {
        return std::nullopt;
    }
    const std::size_t dataOffset =
        header + localHeaderSize + read16(archive, header + 26) + read16(archive, header + 28);
    if (dataOffset + entry.compressedSize > archive.size()) {
        return std::nullopt;
    }
    const auto data = archive.subspan(dataOffset, entry.compressedSize);
    switch (entry.method) {
    case 0:
        return std::vector<std::uint8_t>(data.begin(), data.end());
    case 8:
        return inflate(data, entry.uncompressedSize);
    default:
        return std::nullopt;
    }
}

} // namespace as3lsp::abc
