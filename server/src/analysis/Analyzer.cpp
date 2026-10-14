#include "analysis/Analyzer.hpp"

#include "syntax/Parser.hpp"
#include "util/Strings.hpp"

namespace as3lsp::analysis {

namespace {

using syntax::Token;

std::optional<Location> sourceLocation(const model::Unit* unit, const model::Range& range) {
    if (!unit || !unit->hasSource()) {
        return std::nullopt;
    }
    return Location{unit->uri, range};
}

std::string describeLocal(const model::Local& local) {
    std::string text;
    switch (local.kind) {
    case model::LocalKind::Parameter:
        text = "(parameter) ";
        break;
    case model::LocalKind::Variable:
        text = "var ";
        break;
    case model::LocalKind::Constant:
        text = "const ";
        break;
    case model::LocalKind::Function:
        text = "function " + local.name + model::formatParameters(local.params);
        if (!local.type.empty()) {
            text += ":" + model::displayType(local.type);
        }
        return text;
    }
    text += local.name;
    if (!local.type.empty()) {
        text += ":" + model::displayType(local.type);
    }
    return text;
}

} // namespace

Symbol Analyzer::markupSymbolAt(model::Position position, model::Range* range) const {
    for (const auto& reference : unit_.references) {
        if (!reference.range.contains(position)) {
            continue;
        }
        const auto* type = index_.findType(reference.type);
        if (!type) {
            return {};
        }
        if (range) {
            *range = reference.range;
        }
        if (reference.member.empty()) {
            return {.kind = Symbol::Kind::Type, .type = type};
        }
        if (const auto found = resolver_.findMember(*type, reference.member, MemberScope::Any)) {
            return {.kind = Symbol::Kind::Member, .type = found->owner, .member = found->member};
        }
        return {};
    }
    return {};
}

Symbol Analyzer::symbolAt(model::Position position, model::Range* range) const {
    if (!unit_.isCodeAt(position)) {
        return markupSymbolAt(position, range);
    }
    const auto token = nameAt(document_.offsetAt(position));
    if (!token) {
        return {};
    }
    if (range) {
        *range = tokens_[*token].range();
    }
    return evaluate(*token).symbol;
}

Symbol Analyzer::sourceSymbolOf(const Symbol& symbol) const {
    const bool compiled = (symbol.kind == Symbol::Kind::Type || symbol.kind == Symbol::Kind::Member) &&
                          symbol.type && symbol.type->unit && !symbol.type->unit->hasSource();
    if (!sources_ || !compiled) {
        return symbol;
    }
    const auto* type = sources_->findType(symbol.type->qualifiedName());
    if (!type) {
        return symbol;
    }
    if (symbol.kind == Symbol::Kind::Type) {
        return {.kind = Symbol::Kind::Type, .type = type};
    }
    if (const auto* member = index::SourceLocator::findMember(*type, *symbol.member)) {
        return {.kind = Symbol::Kind::Member, .type = type, .member = member};
    }
    // A member that the source declares in an included file still leads to its class.
    return {.kind = Symbol::Kind::Type, .type = type};
}

std::optional<Location> Analyzer::locationOf(const Symbol& compiledOrSource) const {
    const Symbol symbol = sourceSymbolOf(compiledOrSource);
    switch (symbol.kind) {
    case Symbol::Kind::Local:
        return Location{document_.uri(), symbol.local->selectionRange};
    case Symbol::Kind::Member:
        return sourceLocation(symbol.type->unit, symbol.member->selectionRange);
    case Symbol::Kind::Global:
        return sourceLocation(symbol.global->unit, symbol.global->decl.selectionRange);
    case Symbol::Kind::Type:
        return sourceLocation(symbol.type->unit, symbol.type->selectionRange);
    default:
        return std::nullopt;
    }
}

std::vector<Location> Analyzer::definition(model::Position position) const {
    if (auto location = locationOf(symbolAt(position))) {
        return {std::move(*location)};
    }
    return {};
}

std::vector<Location> Analyzer::typeDefinition(model::Position position) const {
    const Symbol symbol = symbolAt(position);
    std::string_view typeName;
    switch (symbol.kind) {
    case Symbol::Kind::Type:
        if (auto location = locationOf(symbol)) {
            return {std::move(*location)};
        }
        return {};
    case Symbol::Kind::Local:
        typeName = symbol.local->type;
        break;
    case Symbol::Kind::Member:
    case Symbol::Kind::Global:
        typeName =
            symbol.member->isFunction() ? std::string_view{symbol.member->type} : symbol.member->valueType();
        break;
    default:
        return {};
    }
    const TypeRef type = resolver_.resolveTypeRef(typeName, contextOf(symbol));
    if (!type.element.empty()) {
        if (const auto* element = resolver_.resolveType(type.element, type.elementContext)) {
            if (auto location = locationOf({.kind = Symbol::Kind::Type, .type = element})) {
                return {std::move(*location)};
            }
        }
    }
    if (type) {
        if (auto location = locationOf({.kind = Symbol::Kind::Type, .type = type.decl})) {
            return {std::move(*location)};
        }
    }
    return {};
}

std::string Analyzer::describe(const Symbol& symbol) const {
    std::string code;
    std::string footer;
    std::string_view doc;
    switch (symbol.kind) {
    case Symbol::Kind::Type:
        if (!symbol.type->package.empty()) {
            code = "package " + symbol.type->package + "\n";
        }
        code += model::formatType(*symbol.type);
        doc = symbol.type->doc.empty() ? sourceSymbolOf(symbol).type->doc : symbol.type->doc;
        break;
    case Symbol::Kind::Member: {
        code = model::formatSignature(*symbol.member);
        footer = "*in* `" + symbol.type->qualifiedName() + "`";
        doc = symbol.member->doc;
        // Compiled libraries have no documentation, but their source files do.
        if (const Symbol source = sourceSymbolOf(symbol); doc.empty() && source.member) {
            doc = source.member->doc;
        }
        break;
    }
    case Symbol::Kind::Global:
        code = model::formatSignature(symbol.global->decl);
        if (!symbol.global->package.empty()) {
            footer = "*in package* `" + symbol.global->package + "`";
        }
        doc = symbol.global->decl.doc;
        break;
    case Symbol::Kind::Local:
        code = describeLocal(*symbol.local);
        break;
    case Symbol::Kind::Package:
        code = "package " + symbol.package;
        break;
    case Symbol::Kind::None:
        return {};
    }
    std::string markdown = "```actionscript\n" + code + "\n```";
    if (!footer.empty()) {
        markdown += "\n\n" + footer;
    }
    if (!doc.empty()) {
        const std::string text = syntax::formatDocComment(doc);
        if (!text.empty()) {
            markdown += "\n\n---\n\n" + text;
        }
    }
    return markdown;
}

std::optional<Hover> Analyzer::hover(model::Position position) const {
    model::Range range;
    const Symbol symbol = symbolAt(position, &range);
    std::string markdown = describe(symbol);
    if (markdown.empty()) {
        return std::nullopt;
    }
    return Hover{std::move(markdown), range};
}

std::optional<SignatureHelp> Analyzer::signatureHelp(model::Position position) const {
    const std::uint32_t offset = document_.offsetAt(position);
    if (!unit_.isCodeAt(position) || inCommentOrString(offset)) {
        return std::nullopt;
    }
    const auto before = tokenBefore(offset);
    if (!before) {
        return std::nullopt;
    }
    int depth = 0;
    std::uint32_t commas = 0;
    std::optional<std::size_t> open;
    for (std::size_t i = *before + 1; i-- > 0;) {
        const Token& token = tokens_[i];
        if (token.isPunct(")") || token.isPunct("]") || token.isPunct("}")) {
            ++depth;
        } else if (token.isPunct("(") || token.isPunct("[") || token.isPunct("{")) {
            if (depth == 0) {
                if (token.isPunct("(")) {
                    open = i;
                }
                break;
            }
            --depth;
        } else if (depth == 0 && token.isPunct(",")) {
            ++commas;
        } else if (depth == 0 && token.isPunct(";")) {
            break;
        }
    }
    if (!open || *open == 0) {
        return std::nullopt;
    }
    const std::size_t callee = *open - 1;
    if (!tokens_[callee].isName() && !tokens_[callee].isPunct(">")) {
        return std::nullopt;
    }
    if (callee > 0 && (tokens_[callee - 1].isKeyword("function") ||
                       (callee > 1 && tokens_[callee - 2].isKeyword("function")))) {
        return std::nullopt;
    }

    const Value value = evaluate(callee);
    const bool isNew = value.firstToken > 0 && tokens_[value.firstToken - 1].isKeyword("new");
    std::string name;
    std::string returnType;
    std::string_view doc;
    const std::vector<model::Parameter>* params = nullptr;
    static const std::vector<model::Parameter> noParams;

    if (value.kind == Value::Kind::Class && value.type.decl && isNew) {
        const auto* type = value.type.decl;
        const auto* constructor = type->constructor();
        name = type->name;
        params = constructor ? &constructor->params : &noParams;
        doc = constructor && !constructor->doc.empty() ? std::string_view{constructor->doc} : type->doc;
    } else if ((value.symbol.kind == Symbol::Kind::Member || value.symbol.kind == Symbol::Kind::Global) &&
               value.symbol.member->isFunction()) {
        const auto& member = *value.symbol.member;
        name = member.name;
        params = &member.params;
        returnType = member.type;
        doc = member.doc;
    } else if (value.symbol.kind == Symbol::Kind::Local &&
               value.symbol.local->kind == model::LocalKind::Function) {
        const auto& local = *value.symbol.local;
        name = local.name;
        params = &local.params;
        returnType = local.type;
    } else {
        return std::nullopt;
    }

    SignatureHelp help;
    auto& label = help.signature.label;
    label = name + "(";
    for (std::size_t i = 0; i < params->size(); ++i) {
        if (i > 0) {
            label += ", ";
        }
        const auto start = static_cast<std::uint32_t>(util::utf16Length(label));
        label += model::formatParameters({(*params)[i]}).substr(1);
        label.pop_back();
        help.signature.parameters.emplace_back(start, static_cast<std::uint32_t>(util::utf16Length(label)));
    }
    label += ")";
    if (!returnType.empty()) {
        label += ":" + model::displayType(returnType);
    }
    if (!doc.empty()) {
        help.signature.documentation = syntax::formatDocComment(doc);
    }
    help.activeParameter = commas;
    if (!params->empty() && commas >= params->size() && params->back().isRest) {
        help.activeParameter = static_cast<std::uint32_t>(params->size() - 1);
    }
    return help;
}

} // namespace as3lsp::analysis
