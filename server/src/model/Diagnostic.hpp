#pragma once

#include "model/Position.hpp"

#include <cstdint>
#include <string>

namespace as3lsp::model {

// Values match the LSP DiagnosticSeverity enumeration.
enum class Severity : std::uint8_t { Error = 1, Warning = 2 };

struct Diagnostic {
    Range range;
    std::string message;
    Severity severity = Severity::Error;
};

} // namespace as3lsp::model
