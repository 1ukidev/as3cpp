#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace as3lsp::util {

[[nodiscard]] std::string pathToUri(const std::filesystem::path& path);
[[nodiscard]] std::optional<std::filesystem::path> uriToPath(std::string_view uri);

// Stable key for a file, independent of how the URI was encoded by the client.
[[nodiscard]] std::string pathKey(const std::filesystem::path& path);
[[nodiscard]] std::string uriKey(std::string_view uri);

} // namespace as3lsp::util
