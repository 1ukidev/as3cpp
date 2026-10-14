#pragma once

#include "syntax/Token.hpp"

#include <string_view>

namespace as3lsp::syntax {

// Tokens and comments keep views into `source`, which must outlive the result.
[[nodiscard]] LexResult tokenize(std::string_view source);

} // namespace as3lsp::syntax
