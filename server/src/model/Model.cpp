#include "model/Model.hpp"

#include "util/Strings.hpp"

namespace as3lsp::model {

namespace {

bool isIdentifierChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
           c == '$' || static_cast<unsigned char>(c) >= 0x80;
}

std::string qualify(std::string_view package, std::string_view name) {
    if (package.empty()) {
        return std::string{name};
    }
    std::string result;
    result.reserve(package.size() + name.size() + 1);
    result.append(package).push_back('.');
    result.append(name);
    return result;
}

} // namespace

std::string_view Member::valueType() const {
    if (kind == MemberKind::Setter && !params.empty()) {
        return params.front().type;
    }
    if (kind == MemberKind::Method || kind == MemberKind::Constructor) {
        return "Function";
    }
    return type;
}

std::string TypeDecl::qualifiedName() const {
    return qualify(package, name);
}

const Member* TypeDecl::constructor() const {
    for (const auto& member : members) {
        if (member.kind == MemberKind::Constructor) {
            return &member;
        }
    }
    return nullptr;
}

std::string Global::qualifiedName() const {
    return qualify(package, decl.name);
}

void Unit::link() {
    for (auto& type : types) {
        type.unit = this;
    }
    for (auto& global : globals) {
        global.unit = this;
    }
}

const TypeDecl* Unit::typeAt(Position position) const {
    for (const auto& type : types) {
        if (type.range.contains(position)) {
            return &type;
        }
    }
    return nullptr;
}

int Unit::scopeAt(Position position) const {
    // Nested scopes are always stored after their parents, so the last match is the innermost one.
    int found = -1;
    for (int i = 0; i < static_cast<int>(scopes.size()); ++i) {
        if (scopes[static_cast<std::size_t>(i)].range.contains(position)) {
            found = i;
        }
    }
    return found;
}

bool Unit::isCodeAt(Position position) const {
    if (!isMarkup) {
        return true;
    }
    for (const auto& range : codeRanges) {
        if (range.contains(position)) {
            return true;
        }
    }
    return false;
}

std::string displayType(std::string_view type) {
    std::string out;
    out.reserve(type.size());
    std::size_t i = 0;
    while (i < type.size()) {
        if (!isIdentifierChar(type[i])) {
            out.push_back(type[i++]);
            continue;
        }
        const std::size_t start = i;
        while (i < type.size() && (isIdentifierChar(type[i]) ||
                                   (type[i] == '.' && (i + 1 >= type.size() || type[i + 1] != '<')))) {
            ++i;
        }
        out += util::lastSegment(type.substr(start, i - start));
    }
    return out;
}

std::optional<std::string_view> vectorElementType(std::string_view type) {
    for (std::string_view prefix : {std::string_view{"Vector.<"}, std::string_view{"__AS3__.vec.Vector.<"}}) {
        if (type.starts_with(prefix) && type.ends_with('>')) {
            return type.substr(prefix.size(), type.size() - prefix.size() - 1);
        }
    }
    return std::nullopt;
}

std::string_view visibilityKeyword(const Member& member) {
    switch (member.visibility) {
    case Visibility::Public:
        return "public";
    case Visibility::Protected:
        return "protected";
    case Visibility::Private:
        return "private";
    case Visibility::Internal:
        return "internal";
    case Visibility::Namespace:
        return member.ns;
    }
    return {};
}

std::string formatParameters(const std::vector<Parameter>& params) {
    std::string out = "(";
    for (std::size_t i = 0; i < params.size(); ++i) {
        const auto& param = params[i];
        if (i > 0) {
            out += ", ";
        }
        if (param.isRest) {
            out += "...";
        }
        out += param.name;
        if (!param.type.empty()) {
            out += ':';
            out += displayType(param.type);
        }
        if (!param.defaultValue.empty()) {
            out += " = ";
            out += param.defaultValue;
        }
    }
    out += ')';
    return out;
}

std::string formatSignature(const Member& member) {
    std::string out;
    if (const auto keyword = visibilityKeyword(member); !keyword.empty()) {
        out.append(keyword).push_back(' ');
    }
    if (member.isOverride) {
        out += "override ";
    }
    if (member.isStatic) {
        out += "static ";
    }
    if (member.isFinal) {
        out += "final ";
    }
    switch (member.kind) {
    case MemberKind::Variable:
    case MemberKind::Constant:
        out += member.kind == MemberKind::Variable ? "var " : "const ";
        out += member.name;
        if (!member.type.empty()) {
            out += ':';
            out += displayType(member.type);
        }
        return out;
    case MemberKind::Namespace:
        out += "namespace ";
        out += member.name;
        return out;
    case MemberKind::Getter:
        out += "function get ";
        break;
    case MemberKind::Setter:
        out += "function set ";
        break;
    case MemberKind::Method:
    case MemberKind::Constructor:
        out += "function ";
        break;
    }
    out += member.name;
    out += formatParameters(member.params);
    if (member.kind != MemberKind::Constructor && !member.type.empty()) {
        out += ':';
        out += displayType(member.type);
    }
    return out;
}

std::string formatType(const TypeDecl& type) {
    std::string out;
    switch (type.visibility) {
    case Visibility::Public:
        out += "public ";
        break;
    case Visibility::Internal:
        out += "internal ";
        break;
    default:
        break;
    }
    if (type.isFinal) {
        out += "final ";
    }
    if (type.isDynamic) {
        out += "dynamic ";
    }
    out += type.isInterface() ? "interface " : "class ";
    out += type.name;
    if (!type.baseClass.empty()) {
        out += " extends ";
        out += displayType(type.baseClass);
    }
    if (!type.interfaces.empty()) {
        out += type.isInterface() ? " extends " : " implements ";
        for (std::size_t i = 0; i < type.interfaces.size(); ++i) {
            if (i > 0) {
                out += ", ";
            }
            out += displayType(type.interfaces[i]);
        }
    }
    return out;
}

} // namespace as3lsp::model
