#pragma once

#include "model/Diagnostic.hpp"
#include "model/Position.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

namespace as3lsp::syntax {

enum class TokenKind : std::uint8_t {
    Identifier,
    Keyword,
    String,
    Number,
    RegExp,
    Xml,
    Punctuator,
    EndOfFile
};

struct Token {
    TokenKind kind = TokenKind::EndOfFile;
    bool newlineBefore = false;
    // Index into LexResult::comments of a documentation comment placed right before this token.
    std::int32_t docComment = -1;
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    model::Position start;
    model::Position end;
    std::string_view text;

    [[nodiscard]] std::uint32_t endOffset() const { return offset + length; }
    [[nodiscard]] model::Range range() const { return {start, end}; }
    [[nodiscard]] bool isEof() const { return kind == TokenKind::EndOfFile; }
    [[nodiscard]] bool isIdentifier() const { return kind == TokenKind::Identifier; }
    [[nodiscard]] bool isIdentifier(std::string_view name) const { return isIdentifier() && text == name; }
    [[nodiscard]] bool isKeyword(std::string_view name) const {
        return kind == TokenKind::Keyword && text == name;
    }
    [[nodiscard]] bool isPunct(std::string_view punct) const {
        return kind == TokenKind::Punctuator && text == punct;
    }
    // Keywords are valid property names after a dot, so both kinds can name a member.
    [[nodiscard]] bool isName() const { return kind == TokenKind::Identifier || kind == TokenKind::Keyword; }
};

struct Comment {
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    bool isDoc = false;
    std::string_view text;

    [[nodiscard]] std::uint32_t endOffset() const { return offset + length; }
};

struct LexResult {
    // Always terminated by an EndOfFile token.
    std::vector<Token> tokens;
    std::vector<Comment> comments;
    // Unterminated strings and comments.
    std::vector<model::Diagnostic> diagnostics;
};

[[nodiscard]] bool isKeyword(std::string_view word);

} // namespace as3lsp::syntax
