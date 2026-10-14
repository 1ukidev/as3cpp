#include "util/Strings.hpp"

#include <algorithm>

namespace as3lsp::util {

bool iequals(std::string_view a, std::string_view b) {
    return a.size() == b.size() &&
           std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) { return toLower(x) == toLower(y); });
}

bool istartsWith(std::string_view text, std::string_view prefix) {
    return text.size() >= prefix.size() && iequals(text.substr(0, prefix.size()), prefix);
}

bool fuzzyMatch(std::string_view pattern, std::string_view candidate) {
    std::size_t i = 0;
    for (char c : candidate) {
        if (i < pattern.size() && toLower(c) == toLower(pattern[i])) {
            ++i;
        }
    }
    return i == pattern.size();
}

std::string_view trim(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

std::vector<std::string_view> split(std::string_view text, char separator) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    while (true) {
        const auto pos = text.find(separator, start);
        parts.push_back(text.substr(start, pos - start));
        if (pos == std::string_view::npos) {
            return parts;
        }
        start = pos + 1;
    }
}

std::string_view lastSegment(std::string_view qualifiedName) {
    const auto dot = qualifiedName.rfind('.');
    return dot == std::string_view::npos ? qualifiedName : qualifiedName.substr(dot + 1);
}

std::string_view packageOf(std::string_view qualifiedName) {
    const auto dot = qualifiedName.rfind('.');
    return dot == std::string_view::npos ? std::string_view{} : qualifiedName.substr(0, dot);
}

namespace {

// UTF-16 code units produced by the UTF-8 sequence that starts with `lead`.
// Continuation bytes produce zero units, so the sum over a string is its UTF-16 length.
constexpr std::size_t utf16Units(unsigned char lead) {
    if ((lead & 0xC0) == 0x80) {
        return 0;
    }
    return lead >= 0xF0 ? 2 : 1;
}

} // namespace

std::size_t utf16Length(std::string_view utf8) {
    std::size_t units = 0;
    for (char c : utf8) {
        units += utf16Units(static_cast<unsigned char>(c));
    }
    return units;
}

std::size_t utf8OffsetOfUtf16(std::string_view utf8, std::size_t units) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < utf8.size(); ++i) {
        const auto width = utf16Units(static_cast<unsigned char>(utf8[i]));
        if (width == 0) {
            continue;
        }
        if (count >= units) {
            return i;
        }
        count += width;
    }
    return utf8.size();
}

} // namespace as3lsp::util
