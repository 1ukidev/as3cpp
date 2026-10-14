#pragma once

#include "analysis/Analyzer.hpp"
#include "model/Model.hpp"

#include "json/Json.hpp"
#include <optional>
#include <stdexcept>
#include <string>

namespace as3lsp::lsp {

namespace ErrorCode {
constexpr int ParseError = -32700;
constexpr int InvalidRequest = -32600;
constexpr int MethodNotFound = -32601;
constexpr int InvalidParams = -32602;
constexpr int InternalError = -32603;
constexpr int ServerNotInitialized = -32002;
constexpr int RequestCancelled = -32800;
} // namespace ErrorCode

class LspError : public std::runtime_error {
public:
    LspError(int code, const std::string& message) : std::runtime_error(message), code_(code) {}
    [[nodiscard]] int code() const { return code_; }

private:
    int code_;
};

[[nodiscard]] std::optional<model::Position> positionFrom(const json::Value& value);
[[nodiscard]] std::optional<model::Range> rangeFrom(const json::Value& value);

[[nodiscard]] json::Value toJson(model::Position position);
[[nodiscard]] json::Value toJson(const model::Range& range);
[[nodiscard]] json::Value toJson(const analysis::Location& location);
[[nodiscard]] json::Value toJson(const std::vector<analysis::Location>& locations);
[[nodiscard]] json::Value toJson(const analysis::CompletionList& list);
[[nodiscard]] json::Value toJson(const analysis::Hover& hover);
[[nodiscard]] json::Value toJson(const analysis::SignatureHelp& help);

[[nodiscard]] json::Value documentSymbols(const model::Unit& unit);
[[nodiscard]] int symbolKind(const model::Member& member);
[[nodiscard]] int symbolKind(const model::TypeDecl& type);

} // namespace as3lsp::lsp
