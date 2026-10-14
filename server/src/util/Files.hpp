#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace as3lsp::util {

// Reads a whole file as UTF-8 text. A leading byte order mark is removed.
[[nodiscard]] std::optional<std::string> readTextFile(const std::filesystem::path& path);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> readBinaryFile(const std::filesystem::path& path);

} // namespace as3lsp::util
