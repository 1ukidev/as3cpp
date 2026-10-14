#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

namespace as3lsp::util {

[[nodiscard]] constexpr char toLower(char c) {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

[[nodiscard]] constexpr bool isUpper(char c) {
    return c >= 'A' && c <= 'Z';
}

[[nodiscard]] bool iequals(std::string_view a, std::string_view b);
[[nodiscard]] bool istartsWith(std::string_view text, std::string_view prefix);

// Case-insensitive subsequence match, used to filter symbol queries.
[[nodiscard]] bool fuzzyMatch(std::string_view pattern, std::string_view candidate);

[[nodiscard]] std::string_view trim(std::string_view text);
[[nodiscard]] std::vector<std::string_view> split(std::string_view text, char separator);

// Returns the text after the last '.', or the whole text when there is no dot.
[[nodiscard]] std::string_view lastSegment(std::string_view qualifiedName);
// Returns the text before the last '.', or an empty view when there is no dot.
[[nodiscard]] std::string_view packageOf(std::string_view qualifiedName);

[[nodiscard]] std::size_t utf16Length(std::string_view utf8);
// Byte offset inside `utf8` that corresponds to `units` UTF-16 code units. Clamped to the size.
[[nodiscard]] std::size_t utf8OffsetOfUtf16(std::string_view utf8, std::size_t units);

} // namespace as3lsp::util
