#include "analysis/Document.hpp"

#include "syntax/Lexer.hpp"
#include "syntax/Parser.hpp"
#include "util/Uri.hpp"

namespace as3lsp::analysis {

Document::Document(std::string uri, std::string key, std::string text, std::int64_t version,
                   std::shared_ptr<const syntax::MxmlContext> mxml)
    : uri_(std::move(uri)), key_(std::move(key)), text_(std::move(text)), version_(version), lines_(text_),
      mxml_(std::move(mxml)) {}

void Document::setText(std::string text, std::int64_t version) {
    text_ = std::move(text);
    version_ = version;
    lines_ = LineIndex{text_};
    stale_ = true;
}

void Document::applyChange(const model::Range& range, std::string_view text) {
    const std::uint32_t start = lines_.offsetAt(text_, range.start);
    const std::uint32_t end = std::max(start, lines_.offsetAt(text_, range.end));
    text_.replace(start, end - start, text);
    lines_ = LineIndex{text_};
    stale_ = true;
}

bool Document::refresh() {
    if (!stale_) {
        return false;
    }
    std::shared_ptr<model::Unit> unit;
    if (mxml_) {
        const auto path = util::uriToPath(uri_);
        unit = syntax::parseMxml(text_, path.value_or(std::filesystem::path{key_}), *mxml_,
                                 {.collectScopes = true, .collectDiagnostics = true}, &source_, &lex_);
        sourceLines_ = LineIndex{source_};
    } else {
        lex_ = syntax::tokenize(text_);
        unit = syntax::parse(text_, lex_, {.collectScopes = true, .collectDiagnostics = true});
    }
    unit->key = key_;
    unit->uri = uri_;
    unit->origin = model::Origin::Open;
    unit_ = std::move(unit);
    stale_ = false;
    return true;
}

} // namespace as3lsp::analysis
