#pragma once

#include "index/Index.hpp"
#include "model/Model.hpp"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace as3lsp::analysis {

// A resolved type. For "Vector.<T>", `decl` is the vector class and `element` keeps T as written.
struct TypeRef {
    const model::TypeDecl* decl = nullptr;
    std::string element;
    const model::Unit* elementContext = nullptr;

    explicit operator bool() const { return decl != nullptr; }
};

struct MemberRef {
    const model::Member* member = nullptr;
    const model::TypeDecl* owner = nullptr;
};

enum class MemberScope { Instance, Static, Any };

class Resolver {
public:
    explicit Resolver(const index::Index& index) : index_(index) {}

    [[nodiscard]] const index::Index& index() const { return index_; }

    // Resolves a type name as written in `context`, following the AS3 lookup rules:
    // the file itself, explicit imports, the current package, wildcard imports, then the top level.
    [[nodiscard]] const model::TypeDecl* resolveType(std::string_view name, const model::Unit* context) const;
    [[nodiscard]] TypeRef resolveTypeRef(std::string_view name, const model::Unit* context) const;
    [[nodiscard]] const model::Global* resolveGlobal(std::string_view name, const model::Unit* context) const;

    [[nodiscard]] const model::TypeDecl* baseClass(const model::TypeDecl& type) const;
    [[nodiscard]] std::vector<const model::TypeDecl*> supertypes(const model::TypeDecl& type) const;
    [[nodiscard]] bool isSubtypeOf(const model::TypeDecl& type, const model::TypeDecl& base) const;

    // Searches the type and its supertypes. Static members are searched only in the type itself
    // for MemberScope::Static, because AS3 does not inherit static members through class references.
    [[nodiscard]] std::optional<MemberRef> findMember(const model::TypeDecl& type, std::string_view name,
                                                      MemberScope scope) const;

    // Lists the members of a type and its supertypes. A name declared in a subtype hides the inherited one.
    [[nodiscard]] std::vector<MemberRef> members(const model::TypeDecl& type, MemberScope scope) const;

private:
    template <typename Find>
    auto lookup(std::string_view name, const model::Unit* context, Find find) const -> decltype(find(name));

    const index::Index& index_;
};

} // namespace as3lsp::analysis
