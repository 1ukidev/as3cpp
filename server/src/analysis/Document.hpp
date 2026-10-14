#pragma once

#include "analysis/LineIndex.hpp"
#include "model/Model.hpp"
#include "syntax/Mxml.hpp"
#include "syntax/Token.hpp"

#include <cstdint>
#include <memory>
#include <string>

namespace as3lsp::analysis {

// An open text document. Tokens and the declaration model are rebuilt lazily after edits.
// For an MXML document, the analysis uses an ActionScript translation of the text with the same positions.
class Document {
public:
    // `mxml` is required for MXML documents. It is null for ActionScript files.
    Document(std::string uri, std::string key, std::string text, std::int64_t version,
             std::shared_ptr<const syntax::MxmlContext> mxml = nullptr);
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    [[nodiscard]] const std::string& uri() const { return uri_; }
    [[nodiscard]] const std::string& key() const { return key_; }
    [[nodiscard]] const std::string& text() const { return text_; }
    // The ActionScript text that the tokens and positions refer to. Valid only after refresh().
    [[nodiscard]] const std::string& source() const { return mxml_ ? source_ : text_; }
    [[nodiscard]] std::int64_t version() const { return version_; }
    [[nodiscard]] const LineIndex& lines() const { return lines_; }

    void setText(std::string text, std::int64_t version);
    void applyChange(const model::Range& range, std::string_view text);
    void setVersion(std::int64_t version) { version_ = version; }

    // Parses the text again if it changed since the last call. Returns true when the unit was rebuilt.
    bool refresh();
    [[nodiscard]] bool isStale() const { return stale_; }

    // Valid only after refresh().
    [[nodiscard]] const syntax::LexResult& lex() const { return lex_; }
    [[nodiscard]] const model::UnitPtr& unit() const { return unit_; }

    // Offsets in source().
    [[nodiscard]] std::uint32_t offsetAt(model::Position position) const {
        return sourceLines().offsetAt(source(), position);
    }
    [[nodiscard]] model::Position positionAt(std::uint32_t offset) const {
        return sourceLines().positionAt(source(), offset);
    }
    [[nodiscard]] const LineIndex& sourceLines() const { return mxml_ ? sourceLines_ : lines_; }

private:
    std::string uri_;
    std::string key_;
    std::string text_;
    std::int64_t version_ = 0;
    LineIndex lines_;
    std::shared_ptr<const syntax::MxmlContext> mxml_;
    std::string source_;
    LineIndex sourceLines_;
    syntax::LexResult lex_;
    model::UnitPtr unit_;
    bool stale_ = true;
};

} // namespace as3lsp::analysis
