#include "Test.hpp"
#include "abc/Inflate.hpp"
#include "abc/Library.hpp"
#include "index/Project.hpp"

#include <array>
#include <cstdlib>
#include <filesystem>
#include <string>

using namespace as3lsp;

namespace {

std::string asText(const std::vector<std::uint8_t>& bytes) {
    return {bytes.begin(), bytes.end()};
}

std::optional<std::filesystem::path> sdkPath() {
    std::vector<std::filesystem::path> candidates;
    if (const char* env = std::getenv("AS3LSP_SDK")) {
        candidates.emplace_back(env);
    }
    if (const char* home = std::getenv("HOME")) {
        candidates.push_back(std::filesystem::path{home} / "AIRSDK");
    }
    for (const auto& candidate : candidates) {
        if (index::findGlobalLibrary(candidate)) {
            return candidate;
        }
    }
    return std::nullopt;
}

} // namespace

TEST_CASE(inflateDecodesFixedHuffmanBlocks) {
    constexpr std::array<std::uint8_t, 16> zlib = {120, 218, 203, 72, 205, 201, 201, 87,
                                                   200, 64,  39,  1,  104, 3,   8,   177};
    const auto output = abc::zlibDecompress(zlib);
    CHECK(output.has_value());
    CHECK_EQ(asText(*output), "hello hello hello hello");
}

TEST_CASE(inflateDecodesStoredBlocks) {
    constexpr std::array<std::uint8_t, 28> raw = {1,   23,  0,   232, 255, 104, 101, 108, 108, 111,
                                                  32,  104, 101, 108, 108, 111, 32,  104, 101, 108,
                                                  108, 111, 32,  104, 101, 108, 108, 111};
    const auto output = abc::inflate(raw);
    CHECK(output.has_value());
    CHECK_EQ(asText(*output), "hello hello hello hello");
}

TEST_CASE(inflateRejectsTruncatedInput) {
    constexpr std::array<std::uint8_t, 6> zlib = {120, 218, 203, 72, 205, 201};
    CHECK(!abc::zlibDecompress(zlib).has_value());
}

TEST_CASE(libraryLoadsSdkGlobals) {
    const auto sdk = sdkPath();
    if (!sdk) {
        return;
    }
    const auto unit = abc::loadLibrary(*index::findGlobalLibrary(*sdk));
    CHECK(unit != nullptr);
    const model::TypeDecl* sprite = nullptr;
    for (const auto& type : unit->types) {
        if (type.qualifiedName() == "flash.display.Sprite") {
            sprite = &type;
        }
    }
    CHECK(sprite != nullptr);
    CHECK_EQ(sprite->baseClass, "flash.display.DisplayObjectContainer");
    bool hasGraphics = false;
    for (const auto& member : sprite->members) {
        hasGraphics |= member.name == "graphics" && member.kind == model::MemberKind::Getter &&
                       member.type == "flash.display.Graphics";
    }
    CHECK(hasGraphics);
    bool hasTrace = false;
    for (const auto& global : unit->globals) {
        hasTrace |= global.qualifiedName() == "trace";
    }
    CHECK(hasTrace);
}
