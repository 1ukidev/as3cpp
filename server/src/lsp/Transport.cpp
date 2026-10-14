#include "lsp/Transport.hpp"

#include "util/Strings.hpp"

#include <charconv>

namespace as3lsp::lsp {

namespace {

constexpr std::string_view contentLength = "content-length:";

std::optional<std::string> readLine(std::FILE* input) {
    std::string line;
    while (true) {
        const int c = std::getc(input);
        if (c == EOF) {
            return std::nullopt;
        }
        if (c == '\n') {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            return line;
        }
        line.push_back(static_cast<char>(c));
    }
}

} // namespace

std::optional<std::string> Transport::read() {
    std::optional<std::size_t> length;
    while (true) {
        auto line = readLine(input_);
        if (!line) {
            return std::nullopt;
        }
        if (line->empty()) {
            if (length) {
                break;
            }
            continue;
        }
        if (util::istartsWith(*line, contentLength)) {
            const std::string_view value = util::trim(std::string_view{*line}.substr(contentLength.size()));
            std::size_t parsed = 0;
            if (std::from_chars(value.data(), value.data() + value.size(), parsed).ec == std::errc{}) {
                length = parsed;
            }
        }
    }
    std::string body(*length, '\0');
    if (std::fread(body.data(), 1, body.size(), input_) != body.size()) {
        return std::nullopt;
    }
    return body;
}

void Transport::write(std::string_view body) {
    std::scoped_lock lock{writeMutex_};
    std::fprintf(output_, "Content-Length: %zu\r\n\r\n", body.size());
    std::fwrite(body.data(), 1, body.size(), output_);
    std::fflush(output_);
}

} // namespace as3lsp::lsp
