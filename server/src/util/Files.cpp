#include "util/Files.hpp"

#include <fstream>

namespace as3lsp::util {

namespace {

template <typename Container> std::optional<Container> readAll(const std::filesystem::path& path) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        return std::nullopt;
    }
    stream.seekg(0, std::ios::end);
    const auto size = stream.tellg();
    if (size < 0) {
        return std::nullopt;
    }
    stream.seekg(0, std::ios::beg);
    Container data(static_cast<std::size_t>(size), 0);
    if (!stream.read(reinterpret_cast<char*>(data.data()), size)) {
        return std::nullopt;
    }
    return data;
}

} // namespace

std::optional<std::string> readTextFile(const std::filesystem::path& path) {
    auto text = readAll<std::string>(path);
    if (text && text->starts_with("\xEF\xBB\xBF")) {
        text->erase(0, 3);
    }
    return text;
}

std::optional<std::vector<std::uint8_t>> readBinaryFile(const std::filesystem::path& path) {
    return readAll<std::vector<std::uint8_t>>(path);
}

} // namespace as3lsp::util
