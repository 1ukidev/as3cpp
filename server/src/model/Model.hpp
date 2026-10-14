#pragma once

#include "model/Diagnostic.hpp"
#include "model/Position.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace as3lsp::model {

enum class Visibility : std::uint8_t { Public, Protected, Private, Internal, Namespace };

enum class MemberKind : std::uint8_t { Variable, Constant, Method, Getter, Setter, Constructor, Namespace };

enum class TypeKind : std::uint8_t { Class, Interface };

// Where a unit comes from. A higher value wins when two units declare the same name.
enum class Origin : std::uint8_t { Library, Source, Open };

struct Parameter {
    std::string name;
    std::string type;
    std::string defaultValue;
    bool isRest = false;
    Range range;
};

struct Member {
    std::string name;
    MemberKind kind = MemberKind::Variable;
    Visibility visibility = Visibility::Internal;
    bool isStatic = false;
    bool isOverride = false;
    bool isFinal = false;
    std::string ns;
    // Variable type, or return type for functions. Kept as written in the source.
    std::string type;
    std::vector<Parameter> params;
    Range range;
    Range selectionRange;
    std::string doc;

    [[nodiscard]] bool isFunction() const {
        return kind == MemberKind::Method || kind == MemberKind::Constructor;
    }
    [[nodiscard]] bool isAccessor() const { return kind == MemberKind::Getter || kind == MemberKind::Setter; }
    // The type of the value read from this member, for variables and accessors.
    [[nodiscard]] std::string_view valueType() const;
};

struct Unit;

struct TypeDecl {
    std::string name;
    std::string package;
    TypeKind kind = TypeKind::Class;
    Visibility visibility = Visibility::Internal;
    bool isFinal = false;
    bool isDynamic = false;
    // Declared outside the package block, so it is visible only inside its file.
    bool isFilePrivate = false;
    std::string baseClass;
    std::vector<std::string> interfaces;
    std::vector<Member> members;
    Range range;
    Range selectionRange;
    std::string doc;
    const Unit* unit = nullptr;

    [[nodiscard]] std::string qualifiedName() const;
    [[nodiscard]] bool isInterface() const { return kind == TypeKind::Interface; }
    [[nodiscard]] const Member* constructor() const;
};

// A function, variable or constant declared directly inside a package.
struct Global {
    std::string package;
    bool isFilePrivate = false;
    Member decl;
    const Unit* unit = nullptr;

    [[nodiscard]] std::string qualifiedName() const;
};

struct Import {
    // Qualified type name, or the package name for a wildcard import.
    std::string name;
    bool isWildcard = false;
    Range range;
};

enum class LocalKind : std::uint8_t { Variable, Constant, Parameter, Function };

struct Local {
    std::string name;
    std::string type;
    LocalKind kind = LocalKind::Variable;
    std::vector<Parameter> params;
    Range selectionRange;
};

// The body of a function, method or function expression. `var` declarations in AS3 are function scoped.
struct Scope {
    Range range;
    int parent = -1;
    std::vector<Local> locals;
};

// A name in the XML part of an MXML file: a tag that names a class, or an attribute that names a member.
struct MarkupReference {
    Range range;
    // Qualified name of the class.
    std::string type;
    // Member name for an attribute. Empty for a tag.
    std::string member;
};

struct Unit {
    // Stable index key: the normalized file path, or a library identifier.
    std::string key;
    // Document URI. Empty for compiled libraries, which have no source location.
    std::string uri;
    Origin origin = Origin::Source;
    std::string package;
    std::optional<Position> packageBodyStart;
    std::vector<Import> imports;
    std::vector<std::string> openNamespaces;
    std::vector<TypeDecl> types;
    std::vector<Global> globals;
    std::vector<Scope> scopes;

    // MXML files only. Code exists only inside `codeRanges`; the rest of the file is markup.
    bool isMarkup = false;
    std::vector<Range> codeRanges;
    std::vector<MarkupReference> references;
    // Start of the first <fx:Script> block, where an import statement can be inserted.
    std::optional<Position> scriptStart;

    // Syntax errors. Collected only when the parser is asked to.
    std::vector<Diagnostic> diagnostics;

    // Sets the back references from declarations to this unit. Call once the unit has its final address.
    void link();

    [[nodiscard]] bool hasSource() const { return !uri.empty(); }
    [[nodiscard]] const TypeDecl* typeAt(Position position) const;
    [[nodiscard]] int scopeAt(Position position) const;
    // Whether the position is inside ActionScript code. Always true outside MXML files.
    [[nodiscard]] bool isCodeAt(Position position) const;
};

using UnitPtr = std::shared_ptr<const Unit>;

// Removes package qualifiers for display: "flash.display.Sprite" becomes "Sprite",
// "Vector.<flash.geom.Point>" becomes "Vector.<Point>".
[[nodiscard]] std::string displayType(std::string_view type);

// Splits "Vector.<T>" into its element type. Returns nullopt for other types.
[[nodiscard]] std::optional<std::string_view> vectorElementType(std::string_view type);

[[nodiscard]] std::string_view visibilityKeyword(const Member& member);
[[nodiscard]] std::string formatParameters(const std::vector<Parameter>& params);
[[nodiscard]] std::string formatSignature(const Member& member);
[[nodiscard]] std::string formatType(const TypeDecl& type);

} // namespace as3lsp::model
