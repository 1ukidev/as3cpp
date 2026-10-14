#pragma once

#include "model/Model.hpp"

#include <cstdint>
#include <span>

namespace as3lsp::abc {

// Adds the public declarations of an ActionScript Byte Code block to `unit`.
// Returns false when the block is malformed. Declarations read before the error are kept.
bool readAbc(std::span<const std::uint8_t> data, model::Unit& unit);

} // namespace as3lsp::abc
