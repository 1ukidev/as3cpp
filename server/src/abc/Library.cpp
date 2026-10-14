#include "abc/Library.hpp"

#include "abc/AbcReader.hpp"
#include "abc/Inflate.hpp"
#include "abc/Zip.hpp"
#include "util/Files.hpp"
#include "util/Strings.hpp"
#include "util/Uri.hpp"

namespace as3lsp::abc {

namespace {

constexpr std::uint16_t tagEnd = 0;
constexpr std::uint16_t tagDoAbc = 72;
constexpr std::uint16_t tagDoAbc2 = 82;

std::uint32_t read32(std::span<const std::uint8_t> data, std::size_t offset) {
    return static_cast<std::uint32_t>(data[offset]) | (static_cast<std::uint32_t>(data[offset + 1]) << 8) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24);
}

bool readTags(std::span<const std::uint8_t> body, model::Unit& unit) {
    if (body.empty()) {
        return false;
    }
    const std::size_t rectBits = 5 + 4 * static_cast<std::size_t>(body[0] >> 3);
    std::size_t offset = (rectBits + 7) / 8 + 4;
    bool ok = true;
    while (offset + 2 <= body.size()) {
        const std::uint16_t header = static_cast<std::uint16_t>(body[offset] | (body[offset + 1] << 8));
        offset += 2;
        const std::uint16_t code = header >> 6;
        std::size_t length = header & 0x3F;
        if (length == 0x3F) {
            if (offset + 4 > body.size()) {
                return false;
            }
            length = read32(body, offset);
            offset += 4;
        }
        if (code == tagEnd || offset + length > body.size()) {
            break;
        }
        auto tag = body.subspan(offset, length);
        if (code == tagDoAbc2 && tag.size() > 4) {
            tag = tag.subspan(4);
            std::size_t nameEnd = 0;
            while (nameEnd < tag.size() && tag[nameEnd] != 0) {
                ++nameEnd;
            }
            tag = tag.subspan(std::min(nameEnd + 1, tag.size()));
            ok &= readAbc(tag, unit);
        } else if (code == tagDoAbc) {
            ok &= readAbc(tag, unit);
        }
        offset += length;
    }
    return ok;
}

} // namespace

bool readSwf(std::span<const std::uint8_t> movie, model::Unit& unit) {
    if (movie.size() < 8 || movie[1] != 'W' || movie[2] != 'S') {
        return false;
    }
    const std::uint32_t fileLength = read32(movie, 4);
    if (movie[0] == 'F') {
        return readTags(movie.subspan(8), unit);
    }
    if (movie[0] == 'C') {
        const auto body = zlibDecompress(movie.subspan(8), fileLength > 8 ? fileLength - 8 : 0);
        return body && readTags(*body, unit);
    }
    return false;
}

std::shared_ptr<model::Unit> loadLibrary(const std::filesystem::path& path) {
    const auto bytes = util::readBinaryFile(path);
    if (!bytes) {
        return nullptr;
    }
    auto unit = std::make_shared<model::Unit>();
    unit->key = "library:" + util::pathKey(path);
    unit->origin = model::Origin::Library;

    std::string extension = path.extension().string();
    for (char& c : extension) {
        c = util::toLower(c);
    }
    bool ok = false;
    if (extension == ".abc") {
        ok = readAbc(*bytes, *unit);
    } else if (extension == ".swf") {
        ok = readSwf(*bytes, *unit);
    } else if (extension == ".swc") {
        if (const auto entries = listZipEntries(*bytes)) {
            for (const auto& entry : *entries) {
                if (entry.name == "library.swf") {
                    const auto movie = extractZipEntry(*bytes, entry);
                    ok = movie && readSwf(*movie, *unit);
                    break;
                }
            }
        }
    }
    if (!ok && unit->types.empty() && unit->globals.empty()) {
        return nullptr;
    }
    unit->link();
    return unit;
}

} // namespace as3lsp::abc
