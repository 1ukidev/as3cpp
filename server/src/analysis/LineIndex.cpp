#include "analysis/LineIndex.hpp"

#include "util/Strings.hpp"

#include <algorithm>

namespace as3lsp::analysis {

LineIndex::LineIndex(std::string_view text) {
    for (std::uint32_t i = 0; i < text.size(); ++i) {
        if (text[i] == '\n' || (text[i] == '\r' && (i + 1 >= text.size() || text[i + 1] != '\n'))) {
            starts_.push_back(i + 1);
        }
    }
}

std::string_view LineIndex::lineText(std::string_view text, std::uint32_t line) const {
    if (line >= starts_.size()) {
        return {};
    }
    const std::uint32_t start =
        std::min<std::uint32_t>(starts_[line], static_cast<std::uint32_t>(text.size()));
    std::uint32_t end =
        line + 1 < starts_.size() ? starts_[line + 1] : static_cast<std::uint32_t>(text.size());
    while (end > start && (text[end - 1] == '\n' || text[end - 1] == '\r')) {
        --end;
    }
    return text.substr(start, end - start);
}

std::uint32_t LineIndex::offsetAt(std::string_view text, model::Position position) const {
    if (position.line >= starts_.size()) {
        return static_cast<std::uint32_t>(text.size());
    }
    const std::string_view line = lineText(text, position.line);
    return starts_[position.line] +
           static_cast<std::uint32_t>(util::utf8OffsetOfUtf16(line, position.character));
}

model::Position LineIndex::positionAt(std::string_view text, std::uint32_t offset) const {
    offset = std::min<std::uint32_t>(offset, static_cast<std::uint32_t>(text.size()));
    const auto it = std::upper_bound(starts_.begin(), starts_.end(), offset);
    const auto line = static_cast<std::uint32_t>(std::distance(starts_.begin(), it) - 1);
    const std::string_view prefix = text.substr(starts_[line], offset - starts_[line]);
    return {line, static_cast<std::uint32_t>(util::utf16Length(prefix))};
}

} // namespace as3lsp::analysis
