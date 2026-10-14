#pragma once

#include "analysis/Document.hpp"
#include "analysis/Resolver.hpp"
#include "index/Index.hpp"
#include "index/SourceLocator.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace as3lsp::analysis {

struct Location {
    std::string uri;
    model::Range range;
};

struct TextEdit {
    model::Range range;
    std::string newText;
};

// Values match the LSP CompletionItemKind enumeration.
enum class CompletionKind : std::uint8_t {
    Method = 2,
    Function = 3,
    Constructor = 4,
    Field = 5,
    Variable = 6,
    Class = 7,
    Interface = 8,
    Module = 9,
    Property = 10,
    Keyword = 14,
    Constant = 21,
};

struct CompletionItem {
    std::string label;
    CompletionKind kind = CompletionKind::Variable;
    // Shown right after the label, such as a parameter list or a type.
    std::string signature;
    // Shown at the right side, such as the declaring class or package.
    std::string description;
    std::string detail;
    std::string documentation;
    std::string sortText;
    std::optional<TextEdit> importEdit;
};

struct CompletionList {
    std::vector<CompletionItem> items;
    bool isIncomplete = false;
};

struct Hover {
    std::string markdown;
    model::Range range;
};

struct SignatureInfo {
    std::string label;
    std::string documentation;
    // Start and end of each parameter inside the label, in UTF-16 code units.
    std::vector<std::pair<std::uint32_t, std::uint32_t>> parameters;
};

struct SignatureHelp {
    SignatureInfo signature;
    std::uint32_t activeParameter = 0;
};

// The declaration a name refers to.
struct Symbol {
    enum class Kind : std::uint8_t { None, Type, Member, Global, Local, Package };

    Kind kind = Kind::None;
    // The type itself, or the type that declares the member.
    const model::TypeDecl* type = nullptr;
    const model::Member* member = nullptr;
    const model::Global* global = nullptr;
    const model::Local* local = nullptr;
    std::string package{};
};

// The static type information of an expression.
struct Value {
    enum class Kind : std::uint8_t { Unknown, Instance, Class, Package, Function };

    Kind kind = Kind::Unknown;
    TypeRef type;
    std::string package{};
    // Return type of a function value, as written in `context`.
    std::string returnType;
    const model::Unit* context = nullptr;
    // The declaration of the last name of the expression.
    Symbol symbol;
    // Index of the first token of the expression.
    std::size_t firstToken = 0;
};

// Answers language queries for one open document. The document must be refreshed before use.
class Analyzer {
public:
    // `sources` finds the source code of compiled classes. It is optional.
    Analyzer(const index::Index& index, const Document& document,
             const index::SourceLocator* sources = nullptr);

    // `trigger` is the character that triggered the request, or empty when the user invoked it.
    [[nodiscard]] CompletionList completion(model::Position position, std::string_view trigger = {}) const;
    [[nodiscard]] std::vector<Location> definition(model::Position position) const;
    [[nodiscard]] std::vector<Location> typeDefinition(model::Position position) const;
    [[nodiscard]] std::optional<Hover> hover(model::Position position) const;
    [[nodiscard]] std::optional<SignatureHelp> signatureHelp(model::Position position) const;

private:
    friend class CompletionBuilder;

    // Index of the name token that contains `offset`, if any.
    [[nodiscard]] std::optional<std::size_t> nameAt(std::uint32_t offset) const;
    // Index of the last token that starts before `offset`, if any.
    [[nodiscard]] std::optional<std::size_t> tokenBefore(std::uint32_t offset) const;
    [[nodiscard]] bool inCommentOrString(std::uint32_t offset) const;
    [[nodiscard]] std::optional<std::size_t> matchingOpen(std::size_t close, std::size_t floor) const;

    [[nodiscard]] Symbol resolveName(std::string_view name, model::Position position) const;
    [[nodiscard]] Value evaluate(std::size_t last, std::size_t floor = 0) const;
    [[nodiscard]] Value evaluatePrimary(std::size_t token) const;
    [[nodiscard]] Value evaluateParenthesized(std::size_t open, std::size_t close) const;
    [[nodiscard]] Value valueOf(const Symbol& symbol) const;
    [[nodiscard]] Value instanceOf(std::string_view type, const model::Unit* context) const;
    [[nodiscard]] Value memberOf(const Value& receiver, std::string_view name) const;
    [[nodiscard]] Value callOf(const Value& callee) const;
    [[nodiscard]] Value elementOf(const Value& collection) const;
    [[nodiscard]] Symbol symbolAt(model::Position position, model::Range* range = nullptr) const;
    // The class or member named by a tag or an attribute of an MXML file.
    [[nodiscard]] Symbol markupSymbolAt(model::Position position, model::Range* range) const;

    [[nodiscard]] std::optional<Location> locationOf(const Symbol& symbol) const;
    [[nodiscard]] std::string describe(const Symbol& symbol) const;
    [[nodiscard]] const model::TypeDecl* enclosingType(model::Position position) const;
    [[nodiscard]] const model::Unit* contextOf(const Symbol& symbol) const;

    // The declaration of a compiled type or member in its source file, when the source is available.
    [[nodiscard]] Symbol sourceSymbolOf(const Symbol& symbol) const;

    const index::Index& index_;
    const index::SourceLocator* sources_;
    Resolver resolver_;
    const Document& document_;
    const model::Unit& unit_;
    const std::vector<syntax::Token>& tokens_;
};

} // namespace as3lsp::analysis
