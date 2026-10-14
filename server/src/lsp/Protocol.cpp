#include "lsp/Protocol.hpp"

namespace as3lsp::lsp {

namespace SymbolKind {
constexpr int Class = 5;
constexpr int Method = 6;
constexpr int Property = 7;
constexpr int Field = 8;
constexpr int Constructor = 9;
constexpr int Interface = 11;
constexpr int Function = 12;
constexpr int Variable = 13;
constexpr int Constant = 14;
constexpr int Namespace = 3;
} // namespace SymbolKind

std::optional<model::Position> positionFrom(const json::Value& value) {
    const auto* line = value.find("line");
    const auto* character = value.find("character");
    if (!line || !character || !line->isNumber() || !character->isNumber()) {
        return std::nullopt;
    }
    return model::Position{static_cast<std::uint32_t>(line->asInt()),
                           static_cast<std::uint32_t>(character->asInt())};
}

std::optional<model::Range> rangeFrom(const json::Value& value) {
    auto start = positionFrom(value["start"]);
    auto end = positionFrom(value["end"]);
    if (!start || !end) {
        return std::nullopt;
    }
    return model::Range{*start, *end};
}

json::Value toJson(model::Position position) {
    return json::Object{{"line", position.line}, {"character", position.character}};
}

json::Value toJson(const model::Range& range) {
    return json::Object{{"start", toJson(range.start)}, {"end", toJson(range.end)}};
}

json::Value toJson(const analysis::Location& location) {
    return json::Object{{"uri", location.uri}, {"range", toJson(location.range)}};
}

json::Value toJson(const std::vector<analysis::Location>& locations) {
    json::Array array;
    for (const auto& location : locations) {
        array.push_back(toJson(location));
    }
    return array;
}

json::Value toJson(const analysis::CompletionList& list) {
    json::Array items;
    items.reserve(list.items.size());
    for (const auto& item : list.items) {
        json::Value value = json::Object{{"label", item.label}, {"kind", static_cast<int>(item.kind)}};
        if (!item.signature.empty() || !item.description.empty()) {
            json::Value details = json::Object{};
            if (!item.signature.empty()) {
                details["detail"] = item.signature;
            }
            if (!item.description.empty()) {
                details["description"] = item.description;
            }
            value["labelDetails"] = std::move(details);
        }
        if (!item.detail.empty()) {
            value["detail"] = item.detail;
        }
        if (!item.documentation.empty()) {
            value["documentation"] = json::Object{{"kind", "markdown"}, {"value", item.documentation}};
        }
        if (!item.sortText.empty()) {
            value["sortText"] = item.sortText;
        }
        if (item.importEdit) {
            value["additionalTextEdits"] = json::Array{json::Object{{"range", toJson(item.importEdit->range)},
                                                                    {"newText", item.importEdit->newText}}};
        }
        items.push_back(std::move(value));
    }
    return json::Object{{"isIncomplete", list.isIncomplete}, {"items", std::move(items)}};
}

json::Value toJson(const analysis::Hover& hover) {
    return json::Object{{"contents", json::Object{{"kind", "markdown"}, {"value", hover.markdown}}},
                        {"range", toJson(hover.range)}};
}

json::Value toJson(const analysis::SignatureHelp& help) {
    json::Array parameters;
    for (const auto& [start, end] : help.signature.parameters) {
        parameters.push_back(json::Object{{"label", json::Array{start, end}}});
    }
    json::Value signature =
        json::Object{{"label", help.signature.label}, {"parameters", std::move(parameters)}};
    if (!help.signature.documentation.empty()) {
        signature["documentation"] =
            json::Object{{"kind", "markdown"}, {"value", help.signature.documentation}};
    }
    return json::Object{{"signatures", json::Array{std::move(signature)}},
                        {"activeSignature", 0},
                        {"activeParameter", help.activeParameter}};
}

int symbolKind(const model::Member& member) {
    switch (member.kind) {
    case model::MemberKind::Method:
        return SymbolKind::Method;
    case model::MemberKind::Constructor:
        return SymbolKind::Constructor;
    case model::MemberKind::Getter:
    case model::MemberKind::Setter:
        return SymbolKind::Property;
    case model::MemberKind::Constant:
        return SymbolKind::Constant;
    case model::MemberKind::Namespace:
        return SymbolKind::Namespace;
    case model::MemberKind::Variable:
        return SymbolKind::Field;
    }
    return SymbolKind::Field;
}

int symbolKind(const model::TypeDecl& type) {
    return type.isInterface() ? SymbolKind::Interface : SymbolKind::Class;
}

namespace {

json::Value memberSymbol(const model::Member& member, int kind) {
    std::string detail;
    if (member.isFunction() || member.isAccessor()) {
        detail = model::formatParameters(member.params);
    }
    if (!member.type.empty() && member.kind != model::MemberKind::Constructor) {
        detail += ":" + model::displayType(member.type);
    }
    return json::Object{{"name", member.name},
                        {"detail", detail},
                        {"kind", kind},
                        {"range", toJson(member.range)},
                        {"selectionRange", toJson(member.selectionRange)}};
}

} // namespace

json::Value documentSymbols(const model::Unit& unit) {
    json::Array symbols;
    for (const auto& type : unit.types) {
        json::Array children;
        for (const auto& member : type.members) {
            children.push_back(memberSymbol(member, symbolKind(member)));
        }
        std::string detail =
            type.baseClass.empty() ? std::string{} : "extends " + model::displayType(type.baseClass);
        symbols.push_back(json::Object{{"name", type.name},
                                       {"detail", detail},
                                       {"kind", symbolKind(type)},
                                       {"range", toJson(type.range)},
                                       {"selectionRange", toJson(type.selectionRange)},
                                       {"children", std::move(children)}});
    }
    for (const auto& global : unit.globals) {
        const auto& decl = global.decl;
        const int kind = decl.isFunction()                          ? SymbolKind::Function
                         : decl.kind == model::MemberKind::Constant ? SymbolKind::Constant
                                                                    : SymbolKind::Variable;
        symbols.push_back(memberSymbol(decl, kind));
    }
    return symbols;
}

} // namespace as3lsp::lsp
