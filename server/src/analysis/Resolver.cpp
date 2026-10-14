#include "analysis/Resolver.hpp"

#include "util/Strings.hpp"

#include <unordered_set>

namespace as3lsp::analysis {

namespace {

constexpr std::string_view vectorPackage = "__AS3__.vec";
constexpr std::string_view vectorObjectClass = "__AS3__.vec.Vector$object";
constexpr std::string_view vectorClass = "__AS3__.vec.Vector";
constexpr std::size_t maxHierarchyDepth = 64;

std::string qualify(std::string_view package, std::string_view name) {
    if (package.empty()) {
        return std::string{name};
    }
    std::string result{package};
    result.push_back('.');
    result.append(name);
    return result;
}

bool matchesScope(const model::Member& member, MemberScope scope) {
    switch (scope) {
    case MemberScope::Instance:
        return !member.isStatic;
    case MemberScope::Static:
        return member.isStatic;
    case MemberScope::Any:
        return true;
    }
    return false;
}

} // namespace

template <typename Find>
auto Resolver::lookup(std::string_view name, const model::Unit* context, Find find) const
    -> decltype(find(name)) {
    if (name.find('.') != std::string_view::npos) {
        return find(name);
    }
    if (context && context->origin != model::Origin::Library) {
        for (const auto& import : context->imports) {
            if (!import.isWildcard && util::lastSegment(import.name) == name) {
                if (auto result = find(import.name)) {
                    return result;
                }
            }
        }
        if (!context->package.empty()) {
            if (auto result = find(qualify(context->package, name))) {
                return result;
            }
        }
        for (const auto& import : context->imports) {
            if (import.isWildcard) {
                if (auto result = find(qualify(import.name, name))) {
                    return result;
                }
            }
        }
    }
    if (auto result = find(name)) {
        return result;
    }
    return find(qualify(vectorPackage, name));
}

const model::TypeDecl* Resolver::resolveType(std::string_view name, const model::Unit* context) const {
    if (name.empty() || name == "*" || name == "void") {
        return nullptr;
    }
    if (model::vectorElementType(name) || name == "Vector" || name == vectorClass) {
        const auto* vector = index_.findType(vectorObjectClass);
        return vector ? vector : index_.findType(vectorClass);
    }
    if (context && context->origin != model::Origin::Library && name.find('.') == std::string_view::npos) {
        for (const auto& type : context->types) {
            if (type.name == name) {
                return &type;
            }
        }
    }
    return lookup(name, context, [this](std::string_view qualified) { return index_.findType(qualified); });
}

TypeRef Resolver::resolveTypeRef(std::string_view name, const model::Unit* context) const {
    TypeRef ref;
    ref.decl = resolveType(name, context);
    if (const auto element = model::vectorElementType(name)) {
        ref.element = *element;
        ref.elementContext = context;
    }
    return ref;
}

const model::Global* Resolver::resolveGlobal(std::string_view name, const model::Unit* context) const {
    if (name.empty()) {
        return nullptr;
    }
    if (context && name.find('.') == std::string_view::npos) {
        for (const auto& global : context->globals) {
            if (global.decl.name == name) {
                return &global;
            }
        }
    }
    return lookup(name, context, [this](std::string_view qualified) { return index_.findGlobal(qualified); });
}

const model::TypeDecl* Resolver::baseClass(const model::TypeDecl& type) const {
    if (type.isInterface()) {
        return nullptr;
    }
    if (!type.baseClass.empty()) {
        return resolveType(type.baseClass, type.unit);
    }
    if (type.package.empty() && type.name == "Object") {
        return nullptr;
    }
    return index_.findType("Object");
}

std::vector<const model::TypeDecl*> Resolver::supertypes(const model::TypeDecl& type) const {
    std::vector<const model::TypeDecl*> result{&type};
    std::unordered_set<const model::TypeDecl*> visited{&type};
    for (std::size_t i = 0; i < result.size() && result.size() < maxHierarchyDepth; ++i) {
        const model::TypeDecl& current = *result[i];
        const auto push = [&](const model::TypeDecl* parent) {
            if (parent && visited.insert(parent).second) {
                result.push_back(parent);
            }
        };
        push(baseClass(current));
        for (const auto& name : current.interfaces) {
            push(resolveType(name, current.unit));
        }
    }
    return result;
}

bool Resolver::isSubtypeOf(const model::TypeDecl& type, const model::TypeDecl& base) const {
    for (const auto* parent : supertypes(type)) {
        if (parent == &base || parent->qualifiedName() == base.qualifiedName()) {
            return true;
        }
    }
    return false;
}

std::optional<MemberRef> Resolver::findMember(const model::TypeDecl& type, std::string_view name,
                                              MemberScope scope) const {
    const auto search = [&](const model::TypeDecl& owner) -> std::optional<MemberRef> {
        for (const auto& member : owner.members) {
            if (member.name == name && member.kind != model::MemberKind::Constructor &&
                matchesScope(member, scope)) {
                return MemberRef{&member, &owner};
            }
        }
        return std::nullopt;
    };
    if (scope == MemberScope::Static) {
        return search(type);
    }
    for (const auto* owner : supertypes(type)) {
        if (auto found = search(*owner)) {
            return found;
        }
    }
    return std::nullopt;
}

std::vector<MemberRef> Resolver::members(const model::TypeDecl& type, MemberScope scope) const {
    std::vector<MemberRef> result;
    std::unordered_set<std::string_view> seen;
    const auto collect = [&](const model::TypeDecl& owner) {
        for (const auto& member : owner.members) {
            if (member.kind != model::MemberKind::Constructor && matchesScope(member, scope) &&
                seen.insert(member.name).second) {
                result.push_back({&member, &owner});
            }
        }
    };
    if (scope == MemberScope::Static) {
        collect(type);
        return result;
    }
    for (const auto* owner : supertypes(type)) {
        collect(*owner);
    }
    return result;
}

} // namespace as3lsp::analysis
