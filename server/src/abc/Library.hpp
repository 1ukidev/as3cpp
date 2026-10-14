#pragma once

#include "model/Model.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>

namespace as3lsp::abc {

// Loads the declarations of a compiled library: a SWC, SWF or ABC file. Returns nullptr on failure.
[[nodiscard]] std::shared_ptr<model::Unit> loadLibrary(const std::filesystem::path& path);

// Adds the declarations of all ABC blocks in a SWF movie to `unit`.
bool readSwf(std::span<const std::uint8_t> movie, model::Unit& unit);

} // namespace as3lsp::abc
