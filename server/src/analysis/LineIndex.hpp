#pragma once

#include "model/Position.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace as3lsp::analysis {

// Converts between byte offsets and LSP positions (line, UTF-16 column).
class LineIndex {
public:
    LineIndex() = default;
    explicit LineIndex(std::string_view text);

    [[nodiscard]] std::uint32_t offsetAt(std::string_view text, model::Position position) const;
    [[nodiscard]] model::Position positionAt(std::string_view text, std::uint32_t offset) const;
    [[nodiscard]] std::string_view lineText(std::string_view text, std::uint32_t line) const;
    [[nodiscard]] std::size_t lineCount() const { return starts_.size(); }

private:
    std::vector<std::uint32_t> starts_{0};
};

} // namespace as3lsp::analysis
