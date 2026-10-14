#include "syntax/Parser.hpp"

#include "syntax/Lexer.hpp"
#include "util/Strings.hpp"

#include <algorithm>
#include <iterator>
#include <optional>
#include <span>

namespace as3lsp::syntax {

namespace {

using model::MemberKind;
using model::Visibility;

enum class Context { File, Package, Class };

struct Attributes {
    std::size_t firstToken = 0;
    std::optional<Visibility> visibility;
    std::string ns;
    bool isStatic = false;
    bool isFinal = false;
    bool isDynamic = false;
    bool isOverride = false;
    // Conditional compilation block, such as `CONFIG::debug { ... }`.
    bool isConfigBlock = false;
};

bool isVisibilityKeyword(const Token& token) {
    return token.kind == TokenKind::Keyword && (token.text == "public" || token.text == "private" ||
                                                token.text == "protected" || token.text == "internal");
}

bool isModifier(const Token& token) {
    return isVisibilityKeyword(token) ||
           (token.isIdentifier() &&
            (token.text == "static" || token.text == "final" || token.text == "dynamic" ||
             token.text == "override" || token.text == "native"));
}

bool isDeclarationKeyword(const Token& token) {
    return token.isKeyword("class") || token.isKeyword("interface") || token.isKeyword("function") ||
           token.isKeyword("var") || token.isKeyword("const") || token.isIdentifier("namespace");
}

bool startsDeclaration(const Token& token) {
    return isModifier(token) || isDeclarationKeyword(token) || token.isKeyword("import") ||
           token.isKeyword("package") || token.isKeyword("use");
}

// Tokens that can only start a class member, never a statement inside a function body.
// Finding one inside a body means the body is missing its closing brace.
bool startsMemberOnly(const Token& token, const Token& next) {
    return token.newlineBefore && (isVisibilityKeyword(token) || token.isIdentifier("override")) &&
           !next.isPunct("::");
}

bool isOpener(const Token& token) {
    return token.kind == TokenKind::Punctuator &&
           (token.text == "{" || token.text == "(" || token.text == "[");
}

bool isCloser(const Token& token) {
    return token.kind == TokenKind::Punctuator &&
           (token.text == "}" || token.text == ")" || token.text == "]");
}

char closerFor(char opener) {
    switch (opener) {
    case '{':
        return '}';
    case '(':
        return ')';
    default:
        return ']';
    }
}

// Tracks nested brackets. A closing brace also closes any parentheses or brackets left open inside it,
// so a missing ')' while typing does not swallow the rest of the file.
class BracketStack {
public:
    void open(char opener) { stack_.push_back(opener); }

    // Returns false when the closer has no matching opener.
    bool close(char closer) {
        for (auto it = stack_.rbegin(); it != stack_.rend(); ++it) {
            if (closerFor(*it) == closer) {
                stack_.erase(std::prev(it.base()), stack_.end());
                return true;
            }
            if (*it == '{') {
                return false;
            }
        }
        return false;
    }

    [[nodiscard]] std::size_t depth() const { return stack_.size(); }
    [[nodiscard]] bool empty() const { return stack_.empty(); }

private:
    std::vector<char> stack_;
};

class Parser {
public:
    Parser(std::string_view source, const LexResult& lex, const ParseOptions& options)
        : source_(source), tokens_(lex.tokens), comments_(lex.comments), lexDiagnostics_(lex.diagnostics),
          options_(options), unit_(std::make_shared<model::Unit>()) {}

    std::shared_ptr<model::Unit> run() {
        parseDirectives(Context::File, nullptr, false);
        finish(false);
        return std::move(unit_);
    }

    std::shared_ptr<model::Unit> runClassBody(model::TypeDecl type) {
        unit_->package = type.package;
        parseDirectives(Context::Class, &type, false);
        unit_->types.push_back(std::move(type));
        finish(true);
        return std::move(unit_);
    }

private:
    static constexpr std::size_t maxDiagnostics = 100;

    void finish(bool implicitClassBody) {
        if (options_.collectDiagnostics) {
            auto& diagnostics = unit_->diagnostics;
            diagnostics.insert(diagnostics.begin(), lexDiagnostics_.begin(), lexDiagnostics_.end());
            checkBrackets(implicitClassBody);
            std::ranges::stable_sort(diagnostics, {},
                                     [](const model::Diagnostic& d) { return d.range.start; });
            if (diagnostics.size() > maxDiagnostics) {
                diagnostics.resize(maxDiagnostics);
            }
        }
        unit_->link();
    }

    void report(model::Range range, std::string message) {
        if (!options_.collectDiagnostics) {
            return;
        }
        auto& diagnostics = unit_->diagnostics;
        if (!diagnostics.empty() && diagnostics.back().range.start == range.start) {
            return;
        }
        diagnostics.push_back({range, std::move(message)});
    }

    // Reports a missing element. The error goes on the current token, or right after the previous
    // token when the current one starts a new line.
    void expected(std::string_view what) {
        const Token& token = current();
        const model::Range range = token.newlineBefore || token.isEof()
                                       ? model::Range{previous().end, previous().end}
                                       : token.range();
        report(range, "Expected " + std::string{what} + ".");
    }

    // Whether the '{' at `index` opens the body of a package, a class, an interface or a conditional
    // compilation block, where member declarations are allowed.
    [[nodiscard]] bool opensDeclarations(std::size_t index) const {
        if (index >= 2 && tokens_[index - 1].isName() && tokens_[index - 2].isPunct("::")) {
            return true;
        }
        for (std::size_t i = index; i-- > 0 && index - i <= 64;) {
            const Token& token = tokens_[i];
            if (token.isKeyword("class") || token.isKeyword("interface") || token.isKeyword("package")) {
                return true;
            }
            if (token.isPunct("{") || token.isPunct("}") || token.isPunct(";") || token.isPunct(")") ||
                token.isKeyword("function")) {
                return false;
            }
        }
        return false;
    }

    // Reports brackets without a pair. A member declaration inside a function body means that the
    // function is missing its closing brace, so the error goes on the brace of the function.
    void checkBrackets(bool implicitClassBody) {
        struct Open {
            std::size_t token;
            bool opensDeclarations;
        };
        std::vector<Open> stack;
        const auto reportOpen = [&](const Open& open) {
            const Token& token = tokens_[open.token];
            const char closer = closerFor(token.text[0]);
            report(token.range(), "'" + std::string{token.text} + "' is not closed: '" +
                                      std::string(1, closer) + "' is missing.");
        };
        // Reports and removes the openers above `keep`, which stays on the stack.
        const auto closeAbove = [&](std::size_t keep) {
            while (stack.size() > keep + 1) {
                reportOpen(stack.back());
                stack.pop_back();
            }
        };
        const auto innermostBrace = [&]() -> std::optional<std::size_t> {
            for (std::size_t i = stack.size(); i-- > 0;) {
                if (tokens_[stack[i].token].isPunct("{")) {
                    return i;
                }
            }
            return std::nullopt;
        };

        for (std::size_t i = 0; i < tokens_.size(); ++i) {
            const Token& token = tokens_[i];
            if (isOpener(token)) {
                stack.push_back({i, token.isPunct("{") && opensDeclarations(i)});
                continue;
            }
            if (isCloser(token)) {
                const char opener = token.text[0] == '}' ? '{' : token.text[0] == ')' ? '(' : '[';
                std::optional<std::size_t> match;
                for (std::size_t j = stack.size(); j-- > 0;) {
                    const char current = tokens_[stack[j].token].text[0];
                    if (current == opener) {
                        match = j;
                        break;
                    }
                    if (current == '{') {
                        break;
                    }
                }
                if (!match) {
                    report(token.range(), "Unexpected '" + std::string{token.text} +
                                              "': there is no matching '" + std::string(1, opener) + "'.");
                    continue;
                }
                closeAbove(*match);
                stack.pop_back();
                continue;
            }
            if (!startsMemberOnly(token, tokens_[std::min(i + 1, tokens_.size() - 1)])) {
                continue;
            }
            const auto brace = innermostBrace();
            if (!brace || stack[*brace].opensDeclarations) {
                continue;
            }
            std::optional<std::size_t> declarations;
            for (std::size_t j = *brace; j-- > 0;) {
                if (stack[j].opensDeclarations) {
                    declarations = j;
                    break;
                }
            }
            if (declarations) {
                closeAbove(*declarations);
            } else if (implicitClassBody) {
                while (!stack.empty()) {
                    reportOpen(stack.back());
                    stack.pop_back();
                }
            }
        }
        for (auto it = stack.rbegin(); it != stack.rend(); ++it) {
            reportOpen(*it);
        }
    }

    [[nodiscard]] const Token& peek(std::size_t ahead = 0) const {
        const std::size_t index = pos_ + ahead;
        return index < tokens_.size() ? tokens_[index] : tokens_.back();
    }

    [[nodiscard]] const Token& current() const { return peek(); }
    [[nodiscard]] const Token& previous() const { return tokens_[pos_ > 0 ? pos_ - 1 : 0]; }
    [[nodiscard]] bool atEnd() const { return current().isEof(); }

    const Token& advance() {
        const Token& token = current();
        if (!atEnd()) {
            ++pos_;
        }
        return token;
    }

    bool acceptPunct(std::string_view punct) {
        if (current().isPunct(punct)) {
            advance();
            return true;
        }
        return false;
    }

    bool acceptKeyword(std::string_view keyword) {
        if (current().isKeyword(keyword)) {
            advance();
            return true;
        }
        return false;
    }

    [[nodiscard]] model::Position startOf(std::size_t tokenIndex) const { return tokens_[tokenIndex].start; }

    [[nodiscard]] std::string docFor(std::size_t firstToken) {
        const std::int32_t index = pendingDoc_ >= 0 ? pendingDoc_ : tokens_[firstToken].docComment;
        pendingDoc_ = -1;
        return index >= 0 ? std::string{comments_[static_cast<std::size_t>(index)].text} : std::string{};
    }

    // Source text of the tokens in [first, end), with runs of whitespace collapsed.
    [[nodiscard]] std::string textOf(std::size_t first, std::size_t end) const {
        if (first >= end) {
            return {};
        }
        const std::uint32_t from = tokens_[first].offset;
        const std::string_view raw = source_.substr(from, tokens_[end - 1].endOffset() - from);
        std::string text;
        bool space = false;
        for (char c : raw) {
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                space = true;
                continue;
            }
            if (space && !text.empty()) {
                text.push_back(' ');
            }
            space = false;
            text.push_back(c);
        }
        return text;
    }

    void parseDirectives(Context context, model::TypeDecl* owner, bool untilCloseBrace) {
        while (!atEnd()) {
            if (current().isPunct("}")) {
                advance();
                if (untilCloseBrace) {
                    return;
                }
                continue;
            }
            if (untilCloseBrace && context == Context::Class && current().isKeyword("package")) {
                return;
            }
            const std::size_t before = pos_;
            parseDirective(context, owner);
            if (pos_ == before) {
                advance();
            }
        }
    }

    void parseDirective(Context context, model::TypeDecl* owner) {
        const Token& token = current();
        if (token.isPunct("[")) {
            if (pendingDoc_ < 0) {
                pendingDoc_ = token.docComment;
            }
            skipBalanced(false);
            return;
        }
        if (token.isPunct(";")) {
            advance();
            return;
        }
        if (token.isKeyword("package") && context == Context::File) {
            parsePackage();
            return;
        }
        if (token.isKeyword("import")) {
            parseImport();
            return;
        }
        if (token.isKeyword("use")) {
            parseUse();
            return;
        }
        if (token.isIdentifier("include") && peek(1).kind == TokenKind::String) {
            advance();
            advance();
            acceptPunct(";");
            return;
        }

        const std::size_t start = pos_;
        const Attributes attributes = parseAttributes();
        if (attributes.isConfigBlock) {
            advance();
            parseDirectives(context, owner, true);
            return;
        }
        const Token& keyword = current();
        if (keyword.isKeyword("class") || keyword.isKeyword("interface")) {
            parseType(attributes, context);
        } else if (keyword.isKeyword("function")) {
            parseFunction(attributes, context, owner);
        } else if (keyword.isKeyword("var") || keyword.isKeyword("const")) {
            parseVariables(attributes, context, owner);
        } else if (keyword.isIdentifier("namespace") && peek(1).isIdentifier()) {
            parseNamespace(attributes, context, owner);
        } else if (pos_ == start) {
            skipStatement();
        }
        pendingDoc_ = -1;
    }

    void parsePackage() {
        advance();
        if (current().isName()) {
            unit_->package = parseQualifiedName();
        }
        if (current().isPunct("{")) {
            unit_->packageBodyStart = advance().end;
            parseDirectives(Context::Package, nullptr, true);
        }
    }

    std::string parseQualifiedName() {
        std::string name{advance().text};
        while (current().isPunct(".") && peek(1).isName()) {
            advance();
            name.push_back('.');
            name.append(advance().text);
        }
        return name;
    }

    void parseImport() {
        const Token& keyword = advance();
        model::Import import;
        if (!current().isName()) {
            expected("a name after 'import'");
        } else {
            import.name = advance().text;
            while (current().isPunct(".")) {
                advance();
                if (acceptPunct("*")) {
                    import.isWildcard = true;
                    break;
                }
                if (!current().isName()) {
                    expected("a name or '*' after '.'");
                    break;
                }
                import.name.push_back('.');
                import.name.append(advance().text);
            }
        }
        import.range = {keyword.start, previous().end};
        acceptPunct(";");
        if (!import.name.empty()) {
            unit_->imports.push_back(std::move(import));
        }
    }

    void parseUse() {
        advance();
        if (!current().isIdentifier("namespace")) {
            skipStatement();
            return;
        }
        advance();
        while (current().isName()) {
            unit_->openNamespaces.emplace_back(advance().text);
            if (!acceptPunct(",")) {
                break;
            }
        }
        acceptPunct(";");
    }

    Attributes parseAttributes() {
        Attributes attributes;
        attributes.firstToken = pos_;
        while (!atEnd()) {
            const Token& token = current();
            const Token& next = peek(1);
            if (isVisibilityKeyword(token)) {
                attributes.visibility = token.text == "public"      ? Visibility::Public
                                        : token.text == "private"   ? Visibility::Private
                                        : token.text == "protected" ? Visibility::Protected
                                                                    : Visibility::Internal;
                advance();
                continue;
            }
            if (!token.isIdentifier()) {
                break;
            }
            if (next.isPunct("::") && peek(2).isName()) {
                const bool block = peek(3).isPunct("{");
                advance();
                advance();
                advance();
                if (block) {
                    attributes.isConfigBlock = true;
                    return attributes;
                }
                continue;
            }
            const bool followedByDeclaration = isModifier(next) || isDeclarationKeyword(next);
            if (isModifier(token) && followedByDeclaration) {
                attributes.isStatic |= token.text == "static";
                attributes.isFinal |= token.text == "final";
                attributes.isDynamic |= token.text == "dynamic";
                attributes.isOverride |= token.text == "override";
                advance();
                continue;
            }
            if (followedByDeclaration && !next.newlineBefore && token.text != "namespace") {
                attributes.visibility = Visibility::Namespace;
                attributes.ns = token.text;
                advance();
                continue;
            }
            break;
        }
        return attributes;
    }

    void parseType(const Attributes& attributes, Context context) {
        model::TypeDecl type;
        type.kind = advance().isKeyword("interface") ? model::TypeKind::Interface : model::TypeKind::Class;
        type.visibility = attributes.visibility.value_or(Visibility::Internal);
        type.isFinal = attributes.isFinal;
        type.isDynamic = attributes.isDynamic;
        type.isFilePrivate = context == Context::File;
        type.package = context == Context::Package ? unit_->package : std::string{};
        type.doc = docFor(attributes.firstToken);
        if (current().isIdentifier()) {
            type.selectionRange = current().range();
            type.name = advance().text;
        } else {
            expected(type.isInterface() ? "an interface name" : "a class name");
        }
        if (acceptKeyword("extends")) {
            if (type.isInterface()) {
                type.interfaces = parseTypeList();
                if (type.interfaces.empty()) {
                    expected("an interface after 'extends'");
                }
            } else {
                type.baseClass = parseTypeExpression();
                if (type.baseClass.empty()) {
                    expected("a class after 'extends'");
                }
            }
        }
        if (acceptKeyword("implements")) {
            auto interfaces = parseTypeList();
            if (interfaces.empty()) {
                expected("an interface after 'implements'");
            }
            type.interfaces.insert(type.interfaces.end(), interfaces.begin(), interfaces.end());
        }
        if (current().isPunct("{")) {
            advance();
            parseDirectives(Context::Class, &type, true);
        } else {
            expected("'{' to start the body of the " +
                     std::string{type.isInterface() ? "interface" : "class"});
        }
        type.range = {startOf(attributes.firstToken), previous().end};
        if (!type.name.empty()) {
            unit_->types.push_back(std::move(type));
        }
    }

    std::vector<std::string> parseTypeList() {
        std::vector<std::string> types;
        do {
            auto type = parseTypeExpression();
            if (type.empty()) {
                break;
            }
            types.push_back(std::move(type));
        } while (acceptPunct(","));
        return types;
    }

    // The type after ':' in a declaration.
    std::string parseTypeAnnotation() {
        std::string type = parseTypeExpression();
        if (type.empty()) {
            expected("a type after ':'");
        }
        return type;
    }

    std::string parseTypeExpression() {
        if (acceptPunct("*")) {
            return "*";
        }
        if (current().isKeyword("void")) {
            advance();
            return "void";
        }
        if (!current().isName()) {
            return {};
        }
        std::string name = parseQualifiedName();
        if (acceptPunct(".<")) {
            name += ".<";
            name += parseTypeExpression();
            name += '>';
            acceptPunct(">");
        }
        return name;
    }

    void parseFunction(const Attributes& attributes, Context context, model::TypeDecl* owner) {
        const Token& keyword = advance();
        model::Member member;
        member.kind = MemberKind::Method;
        if ((current().isIdentifier("get") || current().isIdentifier("set")) && peek(1).isName()) {
            member.kind = current().text == "get" ? MemberKind::Getter : MemberKind::Setter;
            advance();
        }
        if (current().isName()) {
            member.selectionRange = current().range();
            member.name = advance().text;
        } else {
            expected("a function name");
        }
        if (current().isPunct("(")) {
            member.params = parseParameters();
        } else {
            expected("'(' to start the parameter list");
        }
        if (acceptPunct(":")) {
            member.type = parseTypeAnnotation();
        }
        if (owner && member.kind == MemberKind::Method && member.name == owner->name) {
            member.kind = MemberKind::Constructor;
        }
        applyAttributes(member, attributes, owner);
        member.doc = docFor(attributes.firstToken);

        if (current().isPunct("{")) {
            parseFunctionBody(keyword.start, -1, member.params);
        } else {
            acceptPunct(";");
        }
        member.range = {startOf(attributes.firstToken), previous().end};
        addMember(std::move(member), context, owner);
    }

    void applyAttributes(model::Member& member, const Attributes& attributes,
                         const model::TypeDecl* owner) const {
        const bool inInterface = owner && owner->isInterface();
        member.visibility =
            attributes.visibility.value_or(inInterface ? Visibility::Public : Visibility::Internal);
        member.ns = attributes.ns;
        member.isStatic = attributes.isStatic;
        member.isFinal = attributes.isFinal;
        member.isOverride = attributes.isOverride;
    }

    void addMember(model::Member member, Context context, model::TypeDecl* owner) {
        if (member.name.empty()) {
            return;
        }
        if (owner) {
            owner->members.push_back(std::move(member));
            return;
        }
        model::Global global;
        global.package = context == Context::Package ? unit_->package : std::string{};
        global.isFilePrivate = context == Context::File;
        global.decl = std::move(member);
        unit_->globals.push_back(std::move(global));
    }

    std::vector<model::Parameter> parseParameters() {
        advance();
        std::vector<model::Parameter> params;
        while (!atEnd() && !current().isPunct(")")) {
            model::Parameter param;
            param.isRest = acceptPunct("...");
            if (!current().isName()) {
                if (current().isPunct("{") || current().isPunct(";")) {
                    break;
                }
                expected("a parameter name");
                advance();
                continue;
            }
            param.range = current().range();
            param.name = advance().text;
            if (acceptPunct(":")) {
                param.type = parseTypeAnnotation();
            }
            if (acceptPunct("=")) {
                const std::size_t first = pos_;
                skipExpression(true);
                param.defaultValue = textOf(first, pos_);
            }
            params.push_back(std::move(param));
            if (!acceptPunct(",") && !current().isPunct(")")) {
                if (current().isPunct("{")) {
                    break;
                }
                expected("',' or ')' in the parameter list");
                // A name probably starts the next parameter after a missing comma.
                if (!current().isName()) {
                    advance();
                }
            }
        }
        acceptPunct(")");
        return params;
    }

    void parseVariables(const Attributes& attributes, Context context, model::TypeDecl* owner) {
        const bool isConst = advance().isKeyword("const");
        const std::string doc = docFor(attributes.firstToken);
        if (!current().isName()) {
            expected(isConst ? "a constant name" : "a variable name");
        }
        while (current().isName()) {
            model::Member member;
            member.kind = isConst ? MemberKind::Constant : MemberKind::Variable;
            member.selectionRange = current().range();
            member.name = advance().text;
            if (acceptPunct(":")) {
                member.type = parseTypeAnnotation();
            }
            if (acceptPunct("=")) {
                skipExpression(true);
            }
            applyAttributes(member, attributes, owner);
            member.doc = doc;
            member.range = {startOf(attributes.firstToken), previous().end};
            addMember(std::move(member), context, owner);
            if (!acceptPunct(",")) {
                break;
            }
        }
        acceptPunct(";");
    }

    void parseNamespace(const Attributes& attributes, Context context, model::TypeDecl* owner) {
        advance();
        model::Member member;
        member.kind = MemberKind::Namespace;
        member.type = "Namespace";
        member.selectionRange = current().range();
        member.name = advance().text;
        if (acceptPunct("=")) {
            skipExpression(false);
        }
        acceptPunct(";");
        applyAttributes(member, attributes, owner);
        member.doc = docFor(attributes.firstToken);
        member.range = {startOf(attributes.firstToken), previous().end};
        addMember(std::move(member), context, owner);
    }

    // Skips an initializer or default value. Stops before the token that ends it.
    void skipExpression(bool stopAtComma) {
        const std::size_t start = pos_;
        BracketStack brackets;
        while (!atEnd()) {
            const Token& token = current();
            if (brackets.empty()) {
                if (token.isPunct(";") || isCloser(token) || (stopAtComma && token.isPunct(","))) {
                    return;
                }
                if (pos_ > start && token.newlineBefore && startsDeclaration(token)) {
                    return;
                }
            }
            if (token.isKeyword("function")) {
                parseFunctionExpression(-1);
                continue;
            }
            if (isOpener(token)) {
                brackets.open(token.text[0]);
            } else if (isCloser(token)) {
                brackets.close(token.text[0]);
            }
            advance();
        }
    }

    void skipStatement() {
        const std::size_t start = pos_;
        while (!atEnd()) {
            const Token& token = current();
            if (pos_ > start && token.newlineBefore && startsDeclaration(token)) {
                return;
            }
            if (token.isPunct(";")) {
                advance();
                return;
            }
            if (token.isPunct("}") || token.isPunct(")") || token.isPunct("]")) {
                return;
            }
            if (token.isPunct("{")) {
                skipBalanced(false);
                return;
            }
            if (token.isPunct("(") || token.isPunct("[")) {
                skipBalanced(false);
                continue;
            }
            if (token.isKeyword("function")) {
                parseFunctionExpression(-1);
                continue;
            }
            advance();
        }
    }

    // Skips a bracketed group. The current token must be an opener.
    void skipBalanced(bool isFunctionBody) {
        BracketStack brackets;
        while (!atEnd()) {
            const Token& token = current();
            if (isFunctionBody && brackets.depth() > 0 && startsMemberOnly(token, peek(1))) {
                return;
            }
            if (isOpener(token)) {
                brackets.open(token.text[0]);
            } else if (isCloser(token)) {
                brackets.close(token.text[0]);
            }
            advance();
            if (brackets.empty()) {
                return;
            }
        }
    }

    void parseFunctionBody(model::Position start, int parentScope,
                           const std::vector<model::Parameter>& params) {
        if (!options_.collectScopes) {
            skipBalanced(true);
            return;
        }
        const int scope = addScope(start, parentScope, params);
        scanBody(scope);
        unit_->scopes[static_cast<std::size_t>(scope)].range.end = previous().end;
    }

    void parseFunctionExpression(int parentScope) {
        const Token& keyword = advance();
        model::Local local;
        local.kind = model::LocalKind::Function;
        if (current().isName()) {
            local.selectionRange = current().range();
            local.name = advance().text;
        }
        if (current().isPunct("(")) {
            local.params = parseParameters();
        }
        if (acceptPunct(":")) {
            local.type = parseTypeAnnotation();
        }
        if (current().isPunct("{")) {
            parseFunctionBody(keyword.start, parentScope, local.params);
        }
        if (!local.name.empty() && parentScope >= 0) {
            unit_->scopes[static_cast<std::size_t>(parentScope)].locals.push_back(std::move(local));
        }
    }

    int addScope(model::Position start, int parent, const std::vector<model::Parameter>& params) {
        model::Scope scope;
        scope.range = {start, start};
        scope.parent = parent;
        for (const auto& param : params) {
            model::Local local;
            local.name = param.name;
            local.type = param.isRest && param.type.empty() ? "Array" : param.type;
            local.kind = model::LocalKind::Parameter;
            local.selectionRange = param.range;
            scope.locals.push_back(std::move(local));
        }
        unit_->scopes.push_back(std::move(scope));
        return static_cast<int>(unit_->scopes.size()) - 1;
    }

    void declareLocal(int scope, model::LocalKind kind) {
        if (!current().isName()) {
            return;
        }
        model::Local local;
        local.kind = kind;
        local.selectionRange = current().range();
        local.name = advance().text;
        if (acceptPunct(":")) {
            local.type = parseTypeAnnotation();
        }
        unit_->scopes[static_cast<std::size_t>(scope)].locals.push_back(std::move(local));
    }

    // Scans a function body for local declarations and nested functions. The current token is '{'.
    void scanBody(int scope) {
        BracketStack brackets;
        brackets.open('{');
        advance();
        std::optional<std::size_t> varDepth;
        auto varKind = model::LocalKind::Variable;
        while (!atEnd()) {
            const Token& token = current();
            if (startsMemberOnly(token, peek(1))) {
                return;
            }
            if (token.kind == TokenKind::Punctuator) {
                if (isOpener(token)) {
                    brackets.open(token.text[0]);
                } else if (isCloser(token)) {
                    brackets.close(token.text[0]);
                    if (brackets.empty()) {
                        advance();
                        return;
                    }
                    if (varDepth && brackets.depth() < *varDepth) {
                        varDepth.reset();
                    }
                } else if (token.text == ";") {
                    varDepth.reset();
                } else if (token.text == "," && varDepth == brackets.depth()) {
                    advance();
                    declareLocal(scope, varKind);
                    continue;
                }
            } else if (token.kind == TokenKind::Keyword) {
                if (token.text == "var" || token.text == "const") {
                    varKind = token.text == "var" ? model::LocalKind::Variable : model::LocalKind::Constant;
                    varDepth = brackets.depth();
                    advance();
                    declareLocal(scope, varKind);
                    continue;
                }
                if (token.text == "function") {
                    parseFunctionExpression(scope);
                    continue;
                }
                if (token.text == "catch" && peek(1).isPunct("(")) {
                    advance();
                    brackets.open('(');
                    advance();
                    declareLocal(scope, model::LocalKind::Variable);
                    continue;
                }
            }
            advance();
        }
    }

    std::string_view source_;
    std::span<const Token> tokens_;
    const std::vector<Comment>& comments_;
    const std::vector<model::Diagnostic>& lexDiagnostics_;
    ParseOptions options_;
    std::size_t pos_ = 0;
    std::int32_t pendingDoc_ = -1;
    std::shared_ptr<model::Unit> unit_;
};

void appendDocText(std::string& out, std::string_view text) {
    std::size_t i = 0;
    while (i < text.size()) {
        const char c = text[i];
        if (c == '&') {
            constexpr std::pair<std::string_view, char> entities[] = {
                {"&lt;", '<'}, {"&gt;", '>'}, {"&amp;", '&'}, {"&quot;", '"'}, {"&nbsp;", ' '}};
            bool replaced = false;
            for (const auto& [entity, value] : entities) {
                if (text.substr(i, entity.size()) == entity) {
                    out.push_back(value);
                    i += entity.size();
                    replaced = true;
                    break;
                }
            }
            if (!replaced) {
                out.push_back(c);
                ++i;
            }
            continue;
        }
        const auto close = c == '<' ? text.find('>', i) : std::string_view::npos;
        if (close == std::string_view::npos) {
            out.push_back(c);
            ++i;
            continue;
        }
        std::string tag;
        for (char t : text.substr(i + 1, close - i - 1)) {
            if (t == ' ' || (t == '/' && !tag.empty())) {
                break;
            }
            tag.push_back(util::toLower(t));
        }
        i = close + 1;
        if (tag == "code" || tag == "/code") {
            out.push_back('`');
        } else if (tag == "b" || tag == "/b" || tag == "strong" || tag == "/strong") {
            out += "**";
        } else if (tag == "i" || tag == "/i" || tag == "em" || tag == "/em") {
            out.push_back('*');
        } else if (tag == "p" || tag == "/p" || tag == "ul" || tag == "/ul" || tag == "ol" || tag == "/ol") {
            out += "\n\n";
        } else if (tag == "br") {
            out += "  \n";
        } else if (tag == "li") {
            out += "\n- ";
        } else if (tag == "pre" || tag == "/pre") {
            out += "\n```\n";
        }
    }
}

} // namespace

std::shared_ptr<model::Unit> parse(std::string_view source, const LexResult& lex,
                                   const ParseOptions& options) {
    return Parser{source, lex, options}.run();
}

std::shared_ptr<model::Unit> parseClassBody(std::string_view source, const LexResult& lex,
                                            model::TypeDecl type, const ParseOptions& options) {
    return Parser{source, lex, options}.runClassBody(std::move(type));
}

std::shared_ptr<model::Unit> parse(std::string_view source, const ParseOptions& options) {
    const LexResult lex = tokenize(source);
    return parse(source, lex, options);
}

std::string formatDocComment(std::string_view comment) {
    if (comment.starts_with("/**")) {
        comment.remove_prefix(3);
    }
    if (comment.ends_with("*/")) {
        comment.remove_suffix(2);
    }
    constexpr std::string_view hiddenTags[] = {
        "@langversion",    "@playerversion", "@productversion", "@internal", "@keyword",
        "@includeExample", "@tiptext",       "@helpid",         "@private",  "@inheritDoc"};
    enum class Section { Body, Tag, HiddenTag };
    std::string body;
    std::string tags;
    Section section = Section::Body;
    for (std::string_view line : util::split(comment, '\n')) {
        line = util::trim(line);
        if (line.starts_with('*')) {
            line = util::trim(line.substr(1));
        }
        if (line.starts_with('@')) {
            const auto space = line.find(' ');
            const std::string_view tag = line.substr(0, space);
            std::string_view rest =
                space == std::string_view::npos ? std::string_view{} : line.substr(space + 1);
            if (std::ranges::find(hiddenTags, tag) != std::end(hiddenTags)) {
                section = Section::HiddenTag;
                continue;
            }
            section = Section::Tag;
            tags += "\n\n*";
            tags += tag;
            tags += '*';
            if (tag == "@param") {
                rest = util::trim(rest);
                const auto nameEnd = rest.find(' ');
                tags += " `";
                tags += rest.substr(0, nameEnd);
                tags += '`';
                rest = nameEnd == std::string_view::npos ? std::string_view{} : rest.substr(nameEnd + 1);
            }
            if (!rest.empty()) {
                tags += " — ";
                appendDocText(tags, rest);
            }
            continue;
        }
        if (section == Section::HiddenTag) {
            continue;
        }
        std::string& target = section == Section::Tag ? tags : body;
        if (!target.empty()) {
            target.push_back(section == Section::Tag ? ' ' : '\n');
        }
        appendDocText(target, line);
    }
    std::string result{util::trim(body)};
    if (!tags.empty()) {
        result += tags;
    }
    return result;
}

} // namespace as3lsp::syntax
