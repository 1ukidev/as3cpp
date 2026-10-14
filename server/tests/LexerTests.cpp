#include "Test.hpp"
#include "syntax/Lexer.hpp"

#include <ostream>
#include <string>

using namespace as3lsp;
using syntax::TokenKind;

namespace {

std::vector<std::string> texts(std::string_view source) {
    std::vector<std::string> result;
    for (const auto& token : syntax::tokenize(source).tokens) {
        if (!token.isEof()) {
            result.emplace_back(token.text);
        }
    }
    return result;
}

std::string join(const std::vector<std::string>& parts) {
    std::string out;
    for (const auto& part : parts) {
        out += part + "|";
    }
    return out;
}

} // namespace

TEST_CASE(lexerSeparatesRegexFromDivision) {
    const auto lex = syntax::tokenize("var r = /a\\/b[/]/gi; x = a / b / c;");
    CHECK_EQ(lex.tokens[3].kind, TokenKind::RegExp);
    CHECK_EQ(lex.tokens[3].text, "/a\\/b[/]/gi");
    CHECK_EQ(join(texts("x = a / b / c;")), "x|=|a|/|b|/|c|;|");
}

TEST_CASE(lexerSplitsVectorClosingBrackets) {
    CHECK_EQ(join(texts("Vector.<Vector.<int>>")), "Vector|.<|Vector|.<|int|>|>|");
    CHECK_EQ(join(texts("a >>= 1")), "a|>|>|=|1|");
}

TEST_CASE(lexerReadsXmlLiterals) {
    const auto lex = syntax::tokenize("var x:XML = <a b=\"1\"><c>{value}</c><d/></a>; if (a < b) {}");
    CHECK_EQ(lex.tokens[5].kind, TokenKind::Xml);
    CHECK_EQ(lex.tokens[5].text, "<a b=\"1\"><c>{value}</c><d/></a>");
    CHECK_EQ(lex.tokens[10].text, "<");
}

TEST_CASE(lexerAttachesDocComments) {
    const auto lex = syntax::tokenize("/** Docs. */\n// note\npublic function f():void {}");
    CHECK_EQ(lex.comments.size(), std::size_t{2});
    CHECK(lex.comments[0].isDoc);
    CHECK_EQ(lex.tokens[0].docComment, 0);
    CHECK(lex.tokens[0].newlineBefore);
}

TEST_CASE(lexerTracksUtf16Columns) {
    const auto lex = syntax::tokenize("var s = \"\xC3\xA9\xF0\x9F\x98\x80\"; x\r\n  y");
    CHECK_EQ(lex.tokens[3].end.character, 13u);
    CHECK_EQ(lex.tokens[5].start.character, 15u);
    CHECK_EQ(lex.tokens[6].start.line, 1u);
    CHECK_EQ(lex.tokens[6].start.character, 2u);
}

TEST_CASE(lexerClassifiesKeywords) {
    const auto lex = syntax::tokenize("class get override this");
    CHECK_EQ(lex.tokens[0].kind, TokenKind::Keyword);
    CHECK_EQ(lex.tokens[1].kind, TokenKind::Identifier);
    CHECK_EQ(lex.tokens[2].kind, TokenKind::Identifier);
    CHECK_EQ(lex.tokens[3].kind, TokenKind::Keyword);
}
