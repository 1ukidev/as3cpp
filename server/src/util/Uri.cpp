#include "util/Uri.hpp"

#include "util/Strings.hpp"

#include <array>

namespace as3lsp::util {

namespace {

constexpr std::string_view filePrefix = "file://";

bool isUnreserved(unsigned char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
           c == '.' || c == '_' || c == '~' || c == '/';
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    c = toLower(c);
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

std::string percentDecode(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hexValue(text[i + 1]);
            const int lo = hexValue(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out.push_back(static_cast<char>(hi * 16 + lo));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

bool hasDriveLetter(std::string_view path) {
    return path.size() >= 2 && path[1] == ':' &&
           ((path[0] >= 'a' && path[0] <= 'z') || (path[0] >= 'A' && path[0] <= 'Z'));
}

} // namespace

std::string pathToUri(const std::filesystem::path& path) {
    std::string generic = path.generic_string();
    std::string uri{filePrefix};
    if (!generic.starts_with('/')) {
        uri.push_back('/');
    }
    if (hasDriveLetter(generic)) {
        generic[0] = toLower(generic[0]);
    }
    constexpr std::array<char, 16> digits = {'0', '1', '2', '3', '4', '5', '6', '7',
                                             '8', '9', 'A', 'B', 'C', 'D', 'E', 'F'};
    for (char ch : generic) {
        const auto c = static_cast<unsigned char>(ch);
        if (isUnreserved(c)) {
            uri.push_back(ch);
        } else {
            uri.push_back('%');
            uri.push_back(digits[c >> 4]);
            uri.push_back(digits[c & 0x0F]);
        }
    }
    return uri;
}

std::optional<std::filesystem::path> uriToPath(std::string_view uri) {
    if (!uri.starts_with(filePrefix)) {
        return std::nullopt;
    }
    std::string_view rest = uri.substr(filePrefix.size());
    const auto slash = rest.find('/');
    if (slash == std::string_view::npos) {
        return std::nullopt;
    }
    const std::string_view authority = rest.substr(0, slash);
    std::string path = percentDecode(rest.substr(slash));
    if (path.size() >= 3 && hasDriveLetter(std::string_view{path}.substr(1))) {
        path.erase(0, 1);
    } else if (!authority.empty() && authority != "localhost") {
        path = "//" + std::string{authority} + path;
    }
    return std::filesystem::path{path};
}

std::string pathKey(const std::filesystem::path& path) {
    std::string key = path.lexically_normal().generic_string();
    if (hasDriveLetter(key)) {
        key[0] = toLower(key[0]);
    }
    return key;
}

std::string uriKey(std::string_view uri) {
    if (auto path = uriToPath(uri)) {
        return pathKey(*path);
    }
    return std::string{uri};
}

} // namespace as3lsp::util
