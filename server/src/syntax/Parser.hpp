#pragma once

#include "model/Model.hpp"
#include "syntax/Token.hpp"

#include <memory>
#include <string>
#include <string_view>

namespace as3lsp::syntax {

struct ParseOptions {
    // Function scopes and local declarations are needed only for open documents.
    bool collectScopes = true;
    // Syntax errors are shown only for open documents.
    bool collectDiagnostics = false;
};

// Builds the declaration model of one source file. The parser recovers from syntax errors and never fails.
[[nodiscard]] std::shared_ptr<model::Unit> parse(std::string_view source, const LexResult& lex,
                                                 const ParseOptions& options = {});

[[nodiscard]] std::shared_ptr<model::Unit> parse(std::string_view source, const ParseOptions& options = {});

// Parses the body of one class, such as the script blocks of an MXML file, into `type`.
// The unit takes the package of the type.
[[nodiscard]] std::shared_ptr<model::Unit> parseClassBody(std::string_view source, const LexResult& lex,
                                                          model::TypeDecl type,
                                                          const ParseOptions& options = {});

// Converts an ASDoc comment to Markdown.
[[nodiscard]] std::string formatDocComment(std::string_view comment);

} // namespace as3lsp::syntax
