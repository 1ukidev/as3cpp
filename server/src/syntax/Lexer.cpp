#include "syntax/Lexer.hpp"

#include <algorithm>
#include <array>

namespace as3lsp::syntax {

namespace {

constexpr std::array<std::string_view, 43> keywords = {
    "as",     "break",      "case",    "catch",   "class",      "const",     "continue", "default",
    "delete", "do",         "else",    "extends", "false",      "finally",   "for",      "function",
    "if",     "implements", "import",  "in",      "instanceof", "interface", "internal", "is",
    "new",    "null",       "package", "private", "protected",  "public",    "return",   "super",
    "switch", "this",       "throw",   "true",    "try",        "typeof",    "use",      "var",
    "void",   "while",      "with",
};
static_assert(std::ranges::is_sorted(keywords));

// Longest operators first, so the first match is the longest one. '>' is never combined with other
// characters: the parser reads "Vector.<Vector.<int>>" as two separate closing brackets.
constexpr std::array<std::string_view, 25> operators = {
    "===", "!==", "...", "<<=", "&&=", "||=", "==", "!=", "<=", "<<", "&&", "||", "++",
    "--",  "+=",  "-=",  "*=",  "/=",  "%=",  "&=", "|=", "^=", "::", ".<", "..",
};

constexpr bool isIdentifierStart(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '$' ||
           static_cast<unsigned char>(c) >= 0x80;
}

constexpr bool isDigit(char c) {
    return c >= '0' && c <= '9';
}

constexpr bool isHexDigit(char c) {
    return isDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

constexpr bool isIdentifierPart(char c) {
    return isIdentifierStart(c) || isDigit(c);
}

class Lexer {
public:
    explicit Lexer(std::string_view source) : source_(source) {}

    LexResult run() {
        while (true) {
            const bool newline = skipTrivia();
            const State start = state_;
            if (start.offset >= source_.size()) {
                emit(TokenKind::EndOfFile, start, newline);
                return std::move(result_);
            }
            tokenStart_ = start.offset;
            emit(scanToken(), start, newline);
        }
    }

private:
    struct State {
        std::uint32_t offset = 0;
        model::Position position;
    };

    [[nodiscard]] char peek(std::size_t ahead = 0) const {
        const std::size_t index = state_.offset + ahead;
        return index < source_.size() ? source_[index] : '\0';
    }

    [[nodiscard]] bool atEnd() const { return state_.offset >= source_.size(); }

    [[nodiscard]] bool lookingAt(std::string_view text) const {
        return source_.substr(state_.offset, text.size()) == text;
    }

    void advance() {
        const auto c = static_cast<unsigned char>(source_[state_.offset++]);
        auto& pos = state_.position;
        if (c == '\n' || (c == '\r' && peek() != '\n')) {
            ++pos.line;
            pos.character = 0;
        } else if (c != '\r' && (c & 0xC0) != 0x80) {
            pos.character += c >= 0xF0 ? 2 : 1;
        }
    }

    void advance(std::size_t count) {
        for (std::size_t i = 0; i < count && !atEnd(); ++i) {
            advance();
        }
    }

    void advanceUntil(std::string_view terminator) {
        while (!atEnd() && !lookingAt(terminator)) {
            advance();
        }
        advance(terminator.size());
    }

    void emit(TokenKind kind, const State& start, bool newline) {
        Token token;
        token.kind = kind;
        token.newlineBefore = newline;
        token.docComment = pendingDoc_;
        token.offset = start.offset;
        token.length = state_.offset - start.offset;
        token.start = start.position;
        token.end = state_.position;
        token.text = source_.substr(token.offset, token.length);
        result_.tokens.push_back(token);
        pendingDoc_ = -1;
    }

    // Returns whether a line break was skipped.
    bool skipTrivia() {
        bool newline = result_.tokens.empty();
        while (!atEnd()) {
            const char c = peek();
            if (c == '\n' || c == '\r') {
                newline = true;
                advance();
            } else if (c == ' ' || c == '\t' || c == '\f' || c == '\v') {
                advance();
            } else if (c == '\xC2' && peek(1) == '\xA0') {
                advance(2);
            } else if (c == '/' && peek(1) == '/') {
                const State start = state_;
                while (!atEnd() && peek() != '\n' && peek() != '\r') {
                    advance();
                }
                addComment(start, false);
            } else if (c == '/' && peek(1) == '*') {
                const State start = state_;
                const bool isDoc = peek(2) == '*' && peek(3) != '/';
                advance(2);
                while (!atEnd() && !lookingAt("*/")) {
                    if (peek() == '\n' || peek() == '\r') {
                        newline = true;
                    }
                    advance();
                }
                if (atEnd()) {
                    report(start.position, {start.position.line, start.position.character + 2},
                           "Unterminated comment: '*/' is missing.");
                }
                advance(2);
                addComment(start, isDoc);
            } else {
                break;
            }
        }
        return newline;
    }

    void addComment(const State& start, bool isDoc) {
        Comment comment;
        comment.offset = start.offset;
        comment.length = state_.offset - start.offset;
        comment.isDoc = isDoc;
        comment.text = source_.substr(comment.offset, comment.length);
        if (isDoc) {
            pendingDoc_ = static_cast<std::int32_t>(result_.comments.size());
        }
        result_.comments.push_back(comment);
    }

    TokenKind scanToken() {
        const char c = peek();
        if (isIdentifierStart(c)) {
            while (!atEnd() && isIdentifierPart(peek())) {
                advance();
            }
            return isKeyword(currentText()) ? TokenKind::Keyword : TokenKind::Identifier;
        }
        if (isDigit(c) || (c == '.' && isDigit(peek(1)))) {
            scanNumber();
            return TokenKind::Number;
        }
        if (c == '"' || c == '\'') {
            const model::Position start = state_.position;
            if (!scanString(c)) {
                report(start, state_.position, "Unterminated string: the closing quote is missing.");
            }
            return TokenKind::String;
        }
        if (c == '/' && expressionExpected() && tryScan(&Lexer::scanRegExp)) {
            return TokenKind::RegExp;
        }
        if (c == '<' && expressionExpected() && !previousIs(TokenKind::Keyword, "new") &&
            tryScan(&Lexer::scanXml)) {
            return TokenKind::Xml;
        }
        // In "value:*=null" the '*' is a type, followed by the default value.
        if (c == '*' && previousIs(TokenKind::Punctuator, ":")) {
            advance();
            return TokenKind::Punctuator;
        }
        for (std::string_view op : operators) {
            if (lookingAt(op)) {
                advance(op.size());
                return TokenKind::Punctuator;
            }
        }
        advance();
        return TokenKind::Punctuator;
    }

    [[nodiscard]] std::string_view currentText() const {
        return source_.substr(tokenStart_, state_.offset - tokenStart_);
    }

    bool tryScan(bool (Lexer::*scanner)()) {
        const State saved = state_;
        if ((this->*scanner)()) {
            return true;
        }
        state_ = saved;
        return false;
    }

    [[nodiscard]] bool previousIs(TokenKind kind, std::string_view text) const {
        return !result_.tokens.empty() && result_.tokens.back().kind == kind &&
               result_.tokens.back().text == text;
    }

    // Whether the next token starts an expression, which decides if '/' starts a regular expression
    // and if '<' starts an XML literal.
    [[nodiscard]] bool expressionExpected() const {
        if (result_.tokens.empty()) {
            return true;
        }
        const Token& last = result_.tokens.back();
        switch (last.kind) {
        case TokenKind::Keyword:
            return last.text != "this" && last.text != "super" && last.text != "true" &&
                   last.text != "false" && last.text != "null";
        case TokenKind::Punctuator:
            return last.text != ")" && last.text != "]" && last.text != "++" && last.text != "--" &&
                   last.text != ">";
        default:
            return false;
        }
    }

    void scanNumber() {
        if (peek() == '0' && (peek(1) == 'x' || peek(1) == 'X')) {
            advance(2);
            while (isHexDigit(peek())) {
                advance();
            }
            return;
        }
        while (isDigit(peek())) {
            advance();
        }
        if (peek() == '.' && peek(1) != '.') {
            advance();
            while (isDigit(peek())) {
                advance();
            }
        }
        if ((peek() == 'e' || peek() == 'E') &&
            (isDigit(peek(1)) || ((peek(1) == '+' || peek(1) == '-') && isDigit(peek(2))))) {
            advance(2);
            while (isDigit(peek())) {
                advance();
            }
        }
    }

    // Returns false when the line or the file ends before the closing quote.
    bool scanString(char quote) {
        advance();
        while (!atEnd()) {
            const char c = peek();
            if (c == '\\') {
                advance(2);
            } else if (c == quote) {
                advance();
                return true;
            } else if (c == '\n' || c == '\r') {
                return false;
            } else {
                advance();
            }
        }
        return false;
    }

    void report(model::Position start, model::Position end, std::string message) {
        result_.diagnostics.push_back({{start, end}, std::move(message)});
    }

    bool scanRegExp() {
        advance();
        bool inClass = false;
        while (!atEnd()) {
            const char c = peek();
            if (c == '\n' || c == '\r') {
                return false;
            }
            if (c == '\\') {
                advance(2);
                continue;
            }
            advance();
            if (c == '[') {
                inClass = true;
            } else if (c == ']') {
                inClass = false;
            } else if (c == '/' && !inClass) {
                while (isIdentifierPart(peek())) {
                    advance();
                }
                return true;
            }
        }
        return false;
    }

    bool scanXml() {
        const char next = peek(1);
        if (!isIdentifierStart(next) && next != '>' && next != '{' && next != '!') {
            return false;
        }
        int depth = 0;
        while (!atEnd()) {
            if (lookingAt("<!--")) {
                advanceUntil("-->");
            } else if (lookingAt("<![CDATA[")) {
                advanceUntil("]]>");
            } else if (lookingAt("<?")) {
                advanceUntil("?>");
            } else if (lookingAt("</")) {
                advanceUntil(">");
                if (--depth <= 0) {
                    return depth == 0;
                }
            } else if (peek() == '<') {
                if (!scanXmlTag(depth)) {
                    return false;
                }
                if (depth == 0) {
                    return true;
                }
            } else if (peek() == '{') {
                skipXmlExpression();
            } else {
                advance();
            }
        }
        return false;
    }

    // Scans one start tag. Increments `depth` unless the tag closes itself.
    bool scanXmlTag(int& depth) {
        advance();
        while (!atEnd()) {
            const char c = peek();
            if (c == '"' || c == '\'') {
                scanString(c);
            } else if (c == '{') {
                skipXmlExpression();
            } else if (lookingAt("/>")) {
                advance(2);
                return true;
            } else if (c == '>') {
                advance();
                ++depth;
                return true;
            } else if (c == '<') {
                return false;
            } else {
                advance();
            }
        }
        return false;
    }

    void skipXmlExpression() {
        int depth = 0;
        while (!atEnd()) {
            const char c = peek();
            advance();
            if (c == '{') {
                ++depth;
            } else if (c == '}' && --depth == 0) {
                return;
            }
        }
    }

    std::string_view source_;
    State state_;
    std::uint32_t tokenStart_ = 0;
    std::int32_t pendingDoc_ = -1;
    LexResult result_;
};

} // namespace

bool isKeyword(std::string_view word) {
    return std::ranges::binary_search(keywords, word);
}

LexResult tokenize(std::string_view source) {
    Lexer lexer{source};
    return lexer.run();
}

} // namespace as3lsp::syntax
