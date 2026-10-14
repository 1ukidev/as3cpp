#include "analysis/Analyzer.hpp"
#include "syntax/Parser.hpp"
#include "util/Strings.hpp"

#include <array>
#include <unordered_set>

namespace as3lsp::analysis {

namespace {

using syntax::Token;

constexpr std::array<std::string_view, 37> statementKeywords = {
    "as",    "break",      "case",     "catch", "const",   "continue",  "default",  "delete",
    "do",    "each",       "else",     "false", "finally", "for",       "function", "if",
    "in",    "instanceof", "is",       "new",   "null",    "return",    "super",    "switch",
    "this",  "throw",      "true",     "try",   "typeof",  "undefined", "var",      "void",
    "while", "with",       "Infinity", "NaN",   "import",
};

constexpr std::array<std::string_view, 18> memberKeywords = {
    "public", "private",   "protected", "internal", "static", "override", "final",  "function", "var",
    "const",  "namespace", "import",    "use",      "get",    "set",      "native", "include",  "dynamic",
};

constexpr std::array<std::string_view, 14> packageKeywords = {
    "package", "import",   "class", "interface", "public", "internal",  "final",
    "dynamic", "function", "var",   "const",     "use",    "namespace", "include",
};

enum class TypeFilter { All, Classes };

bool matchesPrefix(std::string_view name, std::string_view prefix) {
    if (prefix.empty()) {
        return true;
    }
    if (!util::fuzzyMatch(prefix, name)) {
        return false;
    }
    const char first = util::toLower(prefix.front());
    if (util::toLower(name.front()) == first) {
        return true;
    }
    for (std::size_t i = 1; i < name.size(); ++i) {
        if ((util::isUpper(name[i]) || name[i - 1] == '_') && util::toLower(name[i]) == first) {
            return true;
        }
    }
    return false;
}

bool isHiddenType(const model::TypeDecl& type) {
    return type.name.find('$') != std::string::npos || type.package.starts_with("__AS3__");
}

CompletionKind memberKind(const model::Member& member) {
    switch (member.kind) {
    case model::MemberKind::Method:
        return CompletionKind::Method;
    case model::MemberKind::Constructor:
        return CompletionKind::Constructor;
    case model::MemberKind::Getter:
    case model::MemberKind::Setter:
        return CompletionKind::Property;
    case model::MemberKind::Constant:
        return CompletionKind::Constant;
    case model::MemberKind::Namespace:
        return CompletionKind::Module;
    case model::MemberKind::Variable:
        return CompletionKind::Field;
    }
    return CompletionKind::Field;
}

std::string memberSignature(const model::Member& member) {
    if (member.isFunction()) {
        std::string text = model::formatParameters(member.params);
        if (!member.type.empty()) {
            text += ":" + model::displayType(member.type);
        }
        return text;
    }
    const std::string_view type = member.valueType();
    return type.empty() ? std::string{} : ":" + model::displayType(type);
}

std::string_view leadingWhitespace(std::string_view line) {
    const auto end = line.find_first_not_of(" \t");
    return end == std::string_view::npos ? line : line.substr(0, end);
}

} // namespace

class CompletionBuilder {
public:
    CompletionBuilder(const Analyzer& analyzer, model::Position position, std::string_view trigger)
        : a_(analyzer), position_(position), trigger_(trigger),
          currentType_(analyzer.enclosingType(position)) {
        if (currentType_) {
            for (const auto* type : a_.resolver_.supertypes(*currentType_)) {
                currentSupertypes_.insert(type->qualifiedName());
            }
        }
    }

    CompletionList build() {
        const std::uint32_t offset = a_.document_.offsetAt(position_);
        if (!a_.unit_.isCodeAt(position_) || a_.inCommentOrString(offset)) {
            return {};
        }
        const auto& tokens = a_.tokens_;
        std::optional<std::size_t> previous = a_.tokenBefore(offset);
        if (previous && tokens[*previous].isName() && tokens[*previous].endOffset() >= offset) {
            const Token& word = tokens[*previous];
            prefix_ = word.text.substr(0, offset - word.offset);
            previous = *previous > 0 ? std::optional{*previous - 1} : std::nullopt;
        }
        if (!previous) {
            if (trigger_.empty()) {
                addGeneral();
            }
            return finish();
        }
        const std::size_t prev = *previous;
        const Token& token = tokens[prev];
        if (!trigger_.empty() && !isTriggerContext(token, prev)) {
            return {};
        }
        if (token.kind == syntax::TokenKind::Number) {
            return {};
        }
        if (token.isPunct(".")) {
            if (prev > 0) {
                addMembersOf(a_.evaluate(prev - 1));
            }
            return finish();
        }
        if (token.isKeyword("import")) {
            addPackageContents("");
            return finish();
        }
        if (token.isKeyword("new")) {
            addTypes(TypeFilter::Classes);
            return finish();
        }
        if (token.isKeyword("extends") || token.isKeyword("implements") || token.isKeyword("as") ||
            token.isKeyword("is") || token.isKeyword("instanceof")) {
            addTypes(TypeFilter::All);
            return finish();
        }
        if (token.isPunct(":") && isTypeAnnotation(prev)) {
            addTypes(TypeFilter::All);
            addKeyword("void");
            addKeyword("*");
            return finish();
        }
        if (declaresName(prev)) {
            return {};
        }
        addGeneral();
        return finish();
    }

private:
    CompletionList finish() {
        list_.isIncomplete = incomplete_;
        return std::move(list_);
    }

    // Trigger characters open the completion list automatically, so they must not do it everywhere.
    [[nodiscard]] bool isTriggerContext(const Token& token, std::size_t prev) const {
        if (trigger_ == ".") {
            return token.isPunct(".");
        }
        if (trigger_ == ":") {
            return token.isPunct(":") && isTypeAnnotation(prev);
        }
        return token.isKeyword("new") || token.isKeyword("import") || token.isKeyword("extends") ||
               token.isKeyword("implements") || token.isKeyword("as") || token.isKeyword("is") ||
               token.isKeyword("instanceof");
    }

    // Whether the previous token starts the declaration of a new name, which has nothing to complete.
    [[nodiscard]] bool declaresName(std::size_t prev) const {
        const Token& token = a_.tokens_[prev];
        if (token.isKeyword("function") || token.isKeyword("var") || token.isKeyword("const") ||
            token.isKeyword("class") || token.isKeyword("interface") || token.isKeyword("package") ||
            token.isIdentifier("namespace")) {
            return true;
        }
        return (token.isIdentifier("get") || token.isIdentifier("set")) && prev > 0 &&
               a_.tokens_[prev - 1].isKeyword("function");
    }

    // Whether the colon at `colon` starts a type annotation, and not an object literal value or a label.
    [[nodiscard]] bool isTypeAnnotation(std::size_t colon) const {
        const auto& tokens = a_.tokens_;
        if (colon == 0) {
            return false;
        }
        const Token& before = tokens[colon - 1];
        if (before.isPunct(")")) {
            const auto open = a_.matchingOpen(colon - 1, 0);
            return open && *open > 0 &&
                   (tokens[*open - 1].isKeyword("function") ||
                    (*open > 1 && tokens[*open - 2].isKeyword("function")) ||
                    (*open > 2 && tokens[*open - 3].isKeyword("function")));
        }
        if (!before.isName() || colon < 2) {
            return false;
        }
        const Token& start = tokens[colon - 2];
        if (start.isKeyword("var") || start.isKeyword("const") || start.isPunct("...")) {
            return true;
        }
        if (!start.isPunct("(") && !start.isPunct(",")) {
            return false;
        }
        // Find the bracket that contains the colon: a parameter list, or the statement of a variable list.
        int depth = 0;
        for (std::size_t i = colon - 1; i-- > 0;) {
            const Token& token = tokens[i];
            if (token.isPunct(")") || token.isPunct("]") || token.isPunct("}")) {
                ++depth;
            } else if (token.isPunct("(") || token.isPunct("[") || token.isPunct("{")) {
                if (depth-- > 0) {
                    continue;
                }
                if (!token.isPunct("(") || i == 0) {
                    return false;
                }
                const Token& callee = tokens[i - 1];
                return callee.isKeyword("function") || callee.isKeyword("catch") ||
                       (callee.isName() && i > 1 &&
                        (tokens[i - 2].isKeyword("function") || tokens[i - 2].isIdentifier("get") ||
                         tokens[i - 2].isIdentifier("set")));
            } else if (depth == 0 && (token.isKeyword("var") || token.isKeyword("const"))) {
                return true;
            } else if (depth == 0 && token.isPunct(";")) {
                return false;
            }
        }
        return false;
    }

    void add(CompletionItem item) {
        if (labels_.insert(item.label).second) {
            list_.items.push_back(std::move(item));
        }
    }

    void addKeyword(std::string_view keyword) {
        if (!matchesPrefix(keyword, prefix_)) {
            return;
        }
        CompletionItem item;
        item.label = keyword;
        item.kind = CompletionKind::Keyword;
        item.sortText = "4" + item.label;
        add(std::move(item));
    }

    [[nodiscard]] bool isAccessible(const model::Member& member, const model::TypeDecl& owner) const {
        switch (member.visibility) {
        case model::Visibility::Public:
            return true;
        case model::Visibility::Private:
            return currentType_ && currentType_->qualifiedName() == owner.qualifiedName();
        case model::Visibility::Protected:
            return currentSupertypes_.contains(owner.qualifiedName());
        case model::Visibility::Internal:
            return owner.package == a_.unit_.package;
        case model::Visibility::Namespace:
            return std::ranges::find(a_.unit_.openNamespaces, member.ns) != a_.unit_.openNamespaces.end() ||
                   currentSupertypes_.contains(owner.qualifiedName());
        }
        return false;
    }

    CompletionItem memberItem(const model::Member& member, std::string_view owner, char category) const {
        CompletionItem item;
        item.label = member.name;
        item.kind = memberKind(member);
        item.signature = memberSignature(member);
        item.description = owner;
        item.detail = model::formatSignature(member);
        if (!member.doc.empty()) {
            item.documentation = syntax::formatDocComment(member.doc);
        }
        item.sortText = category + member.name;
        return item;
    }

    void addMembers(const model::TypeDecl& type, MemberScope scope) {
        for (const auto& ref : a_.resolver_.members(type, scope)) {
            if (isAccessible(*ref.member, *ref.owner)) {
                add(memberItem(*ref.member, model::displayType(ref.owner->name), '1'));
            }
        }
    }

    void addMembersOf(const Value& receiver) {
        switch (receiver.kind) {
        case Value::Kind::Instance:
            if (receiver.type.decl) {
                addMembers(*receiver.type.decl, MemberScope::Instance);
            }
            break;
        case Value::Kind::Class:
            if (receiver.type.decl) {
                addMembers(*receiver.type.decl, MemberScope::Static);
            }
            break;
        case Value::Kind::Package:
            addPackageContents(receiver.package);
            break;
        default:
            break;
        }
    }

    void addPackageContents(const std::string& package) {
        for (const auto& child : a_.index_.subpackages(package)) {
            CompletionItem item;
            item.label = child;
            item.kind = CompletionKind::Module;
            item.sortText = "2" + child;
            add(std::move(item));
        }
        a_.index_.forEachType([&](const model::TypeDecl& type) {
            if (type.package == package && !isHiddenType(type) &&
                (type.visibility == model::Visibility::Public || type.package == a_.unit_.package)) {
                add(typeItem(type, false));
            }
        });
        if (package.empty()) {
            return;
        }
        a_.index_.forEachGlobal([&](const model::Global& global) {
            if (global.package == package && global.decl.visibility == model::Visibility::Public) {
                add(memberItem(global.decl, global.package, '1'));
            }
        });
    }

    CompletionItem typeItem(const model::TypeDecl& type, bool needsImport) {
        CompletionItem item;
        item.label = type.name;
        item.kind = type.isInterface() ? CompletionKind::Interface : CompletionKind::Class;
        item.description = type.package;
        item.detail = model::formatType(type);
        item.sortText = (needsImport ? "5" : "3") + type.name;
        if (needsImport) {
            item.importEdit = importEdit(type.qualifiedName());
        }
        return item;
    }

    void addTypes(TypeFilter filter) {
        for (const auto& type : a_.unit_.types) {
            if (matchesPrefix(type.name, prefix_)) {
                add(typeItem(type, false));
            }
        }
        a_.index_.forEachType([&](const model::TypeDecl& type) {
            if (isHiddenType(type) || (filter == TypeFilter::Classes && type.isInterface())) {
                return;
            }
            if (type.visibility != model::Visibility::Public && type.package != a_.unit_.package) {
                return;
            }
            const bool visible = a_.resolver_.resolveType(type.name, &a_.unit_) == &type;
            if (!visible && prefix_.empty()) {
                incomplete_ = true;
                return;
            }
            if (matchesPrefix(type.name, prefix_)) {
                add(typeItem(type, !visible));
            }
        });
    }

    void addGlobals() {
        for (const auto& global : a_.unit_.globals) {
            add(memberItem(global.decl, {}, '2'));
        }
        a_.index_.forEachGlobal([&](const model::Global& global) {
            if (global.decl.visibility != model::Visibility::Public && global.package != a_.unit_.package) {
                return;
            }
            const bool visible = a_.resolver_.resolveGlobal(global.decl.name, &a_.unit_) == &global;
            if (!visible && prefix_.empty()) {
                incomplete_ = true;
                return;
            }
            if (matchesPrefix(global.decl.name, prefix_)) {
                CompletionItem item = memberItem(global.decl, global.package, visible ? '3' : '5');
                if (!visible) {
                    item.importEdit = importEdit(global.qualifiedName());
                }
                add(std::move(item));
            }
        });
    }

    void addLocals() {
        for (int scope = a_.unit_.scopeAt(position_); scope >= 0;) {
            const auto& current = a_.unit_.scopes[static_cast<std::size_t>(scope)];
            for (const auto& local : current.locals) {
                CompletionItem item;
                item.label = local.name;
                switch (local.kind) {
                case model::LocalKind::Function:
                    item.kind = CompletionKind::Function;
                    item.signature = model::formatParameters(local.params);
                    if (!local.type.empty()) {
                        item.signature += ":" + model::displayType(local.type);
                    }
                    break;
                case model::LocalKind::Constant:
                    item.kind = CompletionKind::Constant;
                    break;
                default:
                    item.kind = CompletionKind::Variable;
                    break;
                }
                if (local.kind != model::LocalKind::Function && !local.type.empty()) {
                    item.signature = ":" + model::displayType(local.type);
                }
                item.sortText = "0" + local.name;
                add(std::move(item));
            }
            scope = current.parent;
        }
    }

    void addGeneral() {
        const bool inFunction = a_.unit_.scopeAt(position_) >= 0;
        if (inFunction) {
            addLocals();
        }
        if (currentType_) {
            addMembers(*currentType_, MemberScope::Any);
        }
        if (inFunction) {
            addGlobals();
            addTypes(TypeFilter::All);
            for (auto keyword : statementKeywords) {
                addKeyword(keyword);
            }
            if (!prefix_.empty()) {
                for (const auto& package : a_.index_.subpackages("")) {
                    if (matchesPrefix(package, prefix_)) {
                        CompletionItem item;
                        item.label = package;
                        item.kind = CompletionKind::Module;
                        item.sortText = "6" + package;
                        add(std::move(item));
                    }
                }
            }
            return;
        }
        addTypes(TypeFilter::All);
        if (currentType_) {
            for (auto keyword : memberKeywords) {
                addKeyword(keyword);
            }
        } else {
            for (auto keyword : packageKeywords) {
                addKeyword(keyword);
            }
        }
    }

    std::optional<TextEdit> importEdit(const std::string& qualifiedName) {
        if (qualifiedName.find('.') == std::string::npos) {
            return std::nullopt;
        }
        const auto& unit = a_.unit_;
        const auto& document = a_.document_;
        TextEdit edit;
        if (!unit.imports.empty()) {
            const auto& last = unit.imports.back();
            const std::string_view line = document.lines().lineText(document.text(), last.range.start.line);
            edit.range.start = {last.range.end.line + 1, 0};
            edit.newText = std::string{leadingWhitespace(line)} + "import " + qualifiedName + ";\n";
        } else if (unit.isMarkup) {
            // In MXML, imports go to the first script block. Without one, there is no place for them.
            if (!unit.scriptStart) {
                return std::nullopt;
            }
            const std::string_view line = document.lines().lineText(document.text(), unit.scriptStart->line);
            edit.range.start = {unit.scriptStart->line + 1, 0};
            edit.newText = std::string{leadingWhitespace(line)} + "\timport " + qualifiedName + ";\n";
        } else {
            const std::uint32_t line = unit.packageBodyStart ? unit.packageBodyStart->line + 1 : 0;
            std::string indent = unit.packageBodyStart ? "\t" : "";
            if (!unit.types.empty()) {
                indent = leadingWhitespace(
                    document.lines().lineText(document.text(), unit.types.front().range.start.line));
            }
            edit.range.start = {line, 0};
            edit.newText = indent + "import " + qualifiedName + ";\n";
            if (!util::trim(document.lines().lineText(document.text(), line)).empty()) {
                edit.newText += "\n";
            }
        }
        edit.range.end = edit.range.start;
        return edit;
    }

    const Analyzer& a_;
    model::Position position_;
    std::string_view trigger_;
    const model::TypeDecl* currentType_;
    std::unordered_set<std::string> currentSupertypes_;
    std::string_view prefix_;
    std::unordered_set<std::string> labels_;
    CompletionList list_;
    bool incomplete_ = false;
};

CompletionList Analyzer::completion(model::Position position, std::string_view trigger) const {
    return CompletionBuilder{*this, position, trigger}.build();
}

} // namespace as3lsp::analysis
