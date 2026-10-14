#include "analysis/Analyzer.hpp"

#include <algorithm>

namespace as3lsp::analysis {

namespace {

using syntax::Token;
using syntax::TokenKind;

bool isOpener(const Token& token) {
    return token.kind == TokenKind::Punctuator &&
           (token.text == "(" || token.text == "[" || token.text == "{");
}

bool isCloser(const Token& token) {
    return token.kind == TokenKind::Punctuator &&
           (token.text == ")" || token.text == "]" || token.text == "}");
}

// Whether the token can end the operand of a member access, call or index expression.
bool endsOperand(const Token& token) {
    if (token.kind == TokenKind::Identifier || token.kind == TokenKind::String ||
        token.kind == TokenKind::Xml) {
        return true;
    }
    if (token.kind == TokenKind::Keyword) {
        return token.text == "this" || token.text == "super";
    }
    return token.isPunct(")") || token.isPunct("]") || token.isPunct(">");
}

std::string joinTokens(const std::vector<Token>& tokens, std::size_t first, std::size_t last) {
    std::string text;
    for (std::size_t i = first; i <= last && i < tokens.size(); ++i) {
        text.append(tokens[i].text);
    }
    return text;
}

} // namespace

Analyzer::Analyzer(const index::Index& index, const Document& document, const index::SourceLocator* sources)
    : index_(index), sources_(sources), resolver_(index), document_(document), unit_(*document.unit()),
      tokens_(document.lex().tokens) {}

std::optional<std::size_t> Analyzer::tokenBefore(std::uint32_t offset) const {
    const auto it =
        std::lower_bound(tokens_.begin(), tokens_.end(), offset,
                         [](const Token& token, std::uint32_t value) { return token.offset < value; });
    if (it == tokens_.begin()) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(std::distance(tokens_.begin(), it)) - 1;
    if (tokens_[index].isEof()) {
        return index > 0 ? std::optional{index - 1} : std::nullopt;
    }
    return index;
}

std::optional<std::size_t> Analyzer::nameAt(std::uint32_t offset) const {
    const auto it =
        std::upper_bound(tokens_.begin(), tokens_.end(), offset,
                         [](std::uint32_t value, const Token& token) { return value < token.offset; });
    if (it == tokens_.begin()) {
        return std::nullopt;
    }
    const auto index = static_cast<std::size_t>(std::distance(tokens_.begin(), it)) - 1;
    if (tokens_[index].isName() && offset <= tokens_[index].endOffset()) {
        return index;
    }
    if (index > 0 && tokens_[index - 1].isName() && tokens_[index - 1].endOffset() == offset) {
        return index - 1;
    }
    return std::nullopt;
}

bool Analyzer::inCommentOrString(std::uint32_t offset) const {
    for (const auto& comment : document_.lex().comments) {
        if (comment.offset >= offset) {
            break;
        }
        const bool lineComment = comment.text.starts_with("//");
        if (offset < comment.endOffset() || (lineComment && offset == comment.endOffset())) {
            return true;
        }
    }
    const auto before = tokenBefore(offset);
    if (!before) {
        return false;
    }
    const Token& token = tokens_[*before];
    if (token.kind != TokenKind::String && token.kind != TokenKind::RegExp && token.kind != TokenKind::Xml) {
        return false;
    }
    if (offset < token.endOffset()) {
        return true;
    }
    const bool terminated = token.length >= 2 && token.text.back() == token.text.front();
    return token.kind == TokenKind::String && !terminated && offset == token.endOffset();
}

std::optional<std::size_t> Analyzer::matchingOpen(std::size_t close, std::size_t floor) const {
    int depth = 0;
    for (std::size_t i = close + 1; i-- > floor;) {
        if (isCloser(tokens_[i])) {
            ++depth;
        } else if (isOpener(tokens_[i]) && --depth == 0) {
            return i;
        }
    }
    return std::nullopt;
}

const model::TypeDecl* Analyzer::enclosingType(model::Position position) const {
    return unit_.typeAt(position);
}

const model::Unit* Analyzer::contextOf(const Symbol& symbol) const {
    switch (symbol.kind) {
    case Symbol::Kind::Member:
    case Symbol::Kind::Type:
        return symbol.type ? symbol.type->unit : nullptr;
    case Symbol::Kind::Global:
        return symbol.global ? symbol.global->unit : nullptr;
    case Symbol::Kind::Local:
        return &unit_;
    default:
        return nullptr;
    }
}

Symbol Analyzer::resolveName(std::string_view name, model::Position position) const {
    for (int scope = unit_.scopeAt(position); scope >= 0;) {
        const auto& current = unit_.scopes[static_cast<std::size_t>(scope)];
        for (const auto& local : current.locals) {
            if (local.name == name) {
                return {.kind = Symbol::Kind::Local, .local = &local};
            }
        }
        scope = current.parent;
    }
    if (const auto* type = enclosingType(position)) {
        if (const auto found = resolver_.findMember(*type, name, MemberScope::Any)) {
            return {.kind = Symbol::Kind::Member, .type = found->owner, .member = found->member};
        }
    }
    if (const auto* type = resolver_.resolveType(name, &unit_)) {
        return {.kind = Symbol::Kind::Type, .type = type};
    }
    if (const auto* global = resolver_.resolveGlobal(name, &unit_)) {
        return {.kind = Symbol::Kind::Global, .member = &global->decl, .global = global};
    }
    if (index_.hasPackage(name)) {
        return {.kind = Symbol::Kind::Package, .package = std::string{name}};
    }
    return {};
}

Value Analyzer::instanceOf(std::string_view type, const model::Unit* context) const {
    Value value;
    value.type = resolver_.resolveTypeRef(type, context);
    if (value.type) {
        value.kind = Value::Kind::Instance;
    }
    return value;
}

Value Analyzer::valueOf(const Symbol& symbol) const {
    Value value;
    switch (symbol.kind) {
    case Symbol::Kind::Local:
        if (symbol.local->kind == model::LocalKind::Function) {
            value.kind = Value::Kind::Function;
            value.returnType = symbol.local->type;
            value.context = &unit_;
        } else {
            value = instanceOf(symbol.local->type, &unit_);
        }
        break;
    case Symbol::Kind::Member:
    case Symbol::Kind::Global: {
        const model::Member& member = *symbol.member;
        if (member.isFunction()) {
            value.kind = Value::Kind::Function;
            value.returnType = member.type;
            value.context = contextOf(symbol);
        } else if (member.kind != model::MemberKind::Namespace) {
            value = instanceOf(member.valueType(), contextOf(symbol));
        }
        break;
    }
    case Symbol::Kind::Type:
        value.kind = Value::Kind::Class;
        value.type.decl = symbol.type;
        break;
    case Symbol::Kind::Package:
        value.kind = Value::Kind::Package;
        value.package = symbol.package;
        break;
    case Symbol::Kind::None:
        break;
    }
    value.symbol = symbol;
    return value;
}

Value Analyzer::memberOf(const Value& receiver, std::string_view name) const {
    switch (receiver.kind) {
    case Value::Kind::Package: {
        const std::string qualified = receiver.package + "." + std::string{name};
        if (const auto* type = index_.findType(qualified)) {
            return valueOf({.kind = Symbol::Kind::Type, .type = type});
        }
        if (const auto* global = index_.findGlobal(qualified)) {
            return valueOf({.kind = Symbol::Kind::Global, .member = &global->decl, .global = global});
        }
        if (index_.hasPackage(qualified)) {
            return valueOf({.kind = Symbol::Kind::Package, .package = qualified});
        }
        return {};
    }
    case Value::Kind::Class:
    case Value::Kind::Instance: {
        if (!receiver.type.decl) {
            return {};
        }
        const auto scope = receiver.kind == Value::Kind::Class ? MemberScope::Static : MemberScope::Instance;
        const auto found = resolver_.findMember(*receiver.type.decl, name, scope);
        if (!found) {
            return {};
        }
        Value value = valueOf({.kind = Symbol::Kind::Member, .type = found->owner, .member = found->member});
        // Vector methods such as pop() are declared to return '*'; they return the element type.
        if (value.kind == Value::Kind::Function && value.returnType == "*" &&
            !receiver.type.element.empty()) {
            value.returnType = receiver.type.element;
            value.context = receiver.type.elementContext;
        }
        return value;
    }
    default:
        return {};
    }
}

Value Analyzer::callOf(const Value& callee) const {
    switch (callee.kind) {
    case Value::Kind::Function:
        return instanceOf(callee.returnType, callee.context);
    case Value::Kind::Class: {
        Value value;
        value.kind = Value::Kind::Instance;
        value.type = callee.type;
        return value;
    }
    default:
        return {};
    }
}

Value Analyzer::elementOf(const Value& collection) const {
    if (collection.kind == Value::Kind::Instance && !collection.type.element.empty()) {
        return instanceOf(collection.type.element, collection.type.elementContext);
    }
    return {};
}

Value Analyzer::evaluatePrimary(std::size_t token) const {
    const Token& name = tokens_[token];
    if (name.isKeyword("this") || name.isKeyword("super")) {
        const auto* type = enclosingType(name.start);
        if (type && name.text == "super") {
            type = resolver_.baseClass(*type);
        }
        if (!type) {
            return {};
        }
        Value value;
        value.kind = Value::Kind::Instance;
        value.type.decl = type;
        value.symbol = {.kind = Symbol::Kind::Type, .type = type};
        return value;
    }
    if (name.kind != TokenKind::Identifier) {
        return {};
    }
    return valueOf(resolveName(name.text, name.start));
}

Value Analyzer::evaluateParenthesized(std::size_t open, std::size_t close) const {
    if (close <= open + 1) {
        return {};
    }
    int depth = 0;
    for (std::size_t i = open + 1; i < close; ++i) {
        if (isOpener(tokens_[i])) {
            ++depth;
        } else if (isCloser(tokens_[i])) {
            --depth;
        } else if (depth == 0 && tokens_[i].isKeyword("as") && i + 1 < close) {
            return instanceOf(joinTokens(tokens_, i + 1, close - 1), &unit_);
        }
    }
    return evaluate(close - 1, open + 1);
}

Value Analyzer::evaluate(std::size_t last, std::size_t floor) const {
    enum class Step : std::uint8_t { Member, Call, Index };
    std::vector<std::pair<Step, std::size_t>> steps;
    std::size_t i = last;
    Value value;
    while (true) {
        const Token& token = tokens_[i];
        if (token.isName()) {
            if (i >= floor + 2 && tokens_[i - 1].isPunct(".") && endsOperand(tokens_[i - 2])) {
                steps.emplace_back(Step::Member, i);
                i -= 2;
                continue;
            }
            value = evaluatePrimary(i);
            break;
        }
        if (token.isPunct(")") || token.isPunct("]")) {
            const auto open = matchingOpen(i, floor);
            if (!open) {
                return {};
            }
            if (*open > floor && endsOperand(tokens_[*open - 1])) {
                steps.emplace_back(token.isPunct(")") ? Step::Call : Step::Index, i);
                i = *open - 1;
                continue;
            }
            value = token.isPunct(")") ? evaluateParenthesized(*open, i) : instanceOf("Array", nullptr);
            i = *open;
            break;
        }
        if (token.isPunct(">")) {
            int depth = 0;
            std::size_t open = i;
            while (open > floor) {
                if (tokens_[open].isPunct(">")) {
                    ++depth;
                } else if (tokens_[open].isPunct(".<") && --depth == 0) {
                    break;
                }
                --open;
            }
            if (depth != 0 || open <= floor || !tokens_[open - 1].isName()) {
                return {};
            }
            i = open - 1;
            value.kind = Value::Kind::Class;
            value.type = resolver_.resolveTypeRef(joinTokens(tokens_, i, last), &unit_);
            value.symbol = {.kind = Symbol::Kind::Type, .type = value.type.decl};
            break;
        }
        switch (token.kind) {
        case TokenKind::String:
            value = instanceOf("String", nullptr);
            break;
        case TokenKind::Number:
            value = instanceOf("Number", nullptr);
            break;
        case TokenKind::RegExp:
            value = instanceOf("RegExp", nullptr);
            break;
        case TokenKind::Xml:
            value = instanceOf("XML", nullptr);
            break;
        default:
            return {};
        }
        break;
    }
    for (auto step = steps.rbegin(); step != steps.rend(); ++step) {
        switch (step->first) {
        case Step::Member:
            value = memberOf(value, tokens_[step->second].text);
            break;
        case Step::Call:
            value = callOf(value);
            break;
        case Step::Index:
            value = elementOf(value);
            break;
        }
    }
    value.firstToken = i;
    return value;
}

} // namespace as3lsp::analysis
