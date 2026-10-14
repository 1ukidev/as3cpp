#pragma once

#include <compare>
#include <cstdint>

namespace as3lsp::model {

// Zero-based line and UTF-16 column, as defined by the LSP specification.
struct Position {
    std::uint32_t line = 0;
    std::uint32_t character = 0;

    auto operator<=>(const Position&) const = default;
};

struct Range {
    Position start;
    Position end;

    [[nodiscard]] bool contains(Position p) const { return start <= p && p <= end; }
    [[nodiscard]] bool containsStrict(Position p) const { return start < p && p < end; }
    bool operator==(const Range&) const = default;
};

} // namespace as3lsp::model
