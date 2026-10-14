#pragma once

#include <cstdio>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace as3lsp::lsp {

// Reads and writes JSON-RPC messages framed with a Content-Length header.
class Transport {
public:
    Transport(std::FILE* input, std::FILE* output) : input_(input), output_(output) {}

    // Blocks until a message arrives. Returns nullopt when the input is closed or malformed.
    [[nodiscard]] std::optional<std::string> read();
    void write(std::string_view body);

private:
    std::FILE* input_;
    std::FILE* output_;
    std::mutex writeMutex_;
};

} // namespace as3lsp::lsp
