#include "json/Json.hpp"

#include <charconv>
#include <cmath>
#include <cstdio>

namespace as3lsp::json {

const Value* Object::find(std::string_view key) const {
    for (const auto& [name, value] : entries_) {
        if (name == key) {
            return &value;
        }
    }
    return nullptr;
}

Value* Object::find(std::string_view key) {
    return const_cast<Value*>(std::as_const(*this).find(key));
}

Value& Object::operator[](std::string_view key) {
    if (auto* value = find(key)) {
        return *value;
    }
    return entries_.emplace_back(std::string{key}, Value{}).second;
}

bool Value::asBool(bool fallback) const {
    const auto* value = std::get_if<bool>(&data_);
    return value ? *value : fallback;
}

std::int64_t Value::asInt(std::int64_t fallback) const {
    if (const auto* value = std::get_if<std::int64_t>(&data_)) {
        return *value;
    }
    if (const auto* value = std::get_if<double>(&data_)) {
        return static_cast<std::int64_t>(*value);
    }
    return fallback;
}

double Value::asDouble(double fallback) const {
    if (const auto* value = std::get_if<double>(&data_)) {
        return *value;
    }
    if (const auto* value = std::get_if<std::int64_t>(&data_)) {
        return static_cast<double>(*value);
    }
    return fallback;
}

std::string_view Value::asString(std::string_view fallback) const {
    const auto* value = std::get_if<std::string>(&data_);
    return value ? std::string_view{*value} : fallback;
}

const Value* Value::find(std::string_view key) const {
    const auto* object = std::get_if<Object>(&data_);
    return object ? object->find(key) : nullptr;
}

const Value& Value::operator[](std::string_view key) const {
    static const Value null;
    const auto* value = find(key);
    return value ? *value : null;
}

Value& Value::operator[](std::string_view key) {
    if (isNull()) {
        data_ = Object{};
    }
    return std::get<Object>(data_)[key];
}

void Value::push(Value value) {
    if (isNull()) {
        data_ = Array{};
    }
    std::get<Array>(data_).push_back(std::move(value));
}

bool Value::operator==(const Value& other) const {
    if (isNumber() && other.isNumber()) {
        return asDouble() == other.asDouble();
    }
    if (data_.index() != other.data_.index()) {
        return false;
    }
    if (isObject()) {
        const auto& a = object();
        const auto& b = other.object();
        if (a.size() != b.size()) {
            return false;
        }
        for (const auto& [key, value] : a) {
            const auto* match = b.find(key);
            if (!match || !(*match == value)) {
                return false;
            }
        }
        return true;
    }
    if (isArray()) {
        return array() == other.array();
    }
    if (isString()) {
        return asString() == other.asString();
    }
    return isNull() || asBool() == other.asBool();
}

namespace {

constexpr int maxDepth = 256;

class Parser {
public:
    Parser(std::string_view text, ParseOptions options) : text_(text), options_(options) {}

    std::optional<Value> parseDocument() {
        auto value = parseValue(0);
        skipWhitespace();
        if (!value || pos_ != text_.size()) {
            return std::nullopt;
        }
        return value;
    }

private:
    std::optional<Value> parseValue(int depth) {
        if (depth > maxDepth) {
            return std::nullopt;
        }
        skipWhitespace();
        if (pos_ >= text_.size()) {
            return std::nullopt;
        }
        switch (text_[pos_]) {
        case '{':
            return parseObject(depth);
        case '[':
            return parseArray(depth);
        case '"': {
            auto text = parseString();
            return text ? std::optional<Value>{std::move(*text)} : std::nullopt;
        }
        case 't':
            return parseLiteral("true", Value{true});
        case 'f':
            return parseLiteral("false", Value{false});
        case 'n':
            return parseLiteral("null", Value{nullptr});
        default:
            return parseNumber();
        }
    }

    std::optional<Value> parseLiteral(std::string_view word, Value value) {
        if (text_.substr(pos_, word.size()) != word) {
            return std::nullopt;
        }
        pos_ += word.size();
        return value;
    }

    std::optional<Value> parseObject(int depth) {
        ++pos_;
        Object object;
        skipWhitespace();
        if (consume('}')) {
            return Value{std::move(object)};
        }
        while (true) {
            skipWhitespace();
            if (options_.lenient && consume('}')) {
                return Value{std::move(object)};
            }
            if (pos_ >= text_.size() || text_[pos_] != '"') {
                return std::nullopt;
            }
            auto key = parseString();
            if (!key) {
                return std::nullopt;
            }
            skipWhitespace();
            if (!consume(':')) {
                return std::nullopt;
            }
            auto value = parseValue(depth + 1);
            if (!value) {
                return std::nullopt;
            }
            object[*key] = std::move(*value);
            skipWhitespace();
            if (consume(',')) {
                continue;
            }
            if (consume('}')) {
                return Value{std::move(object)};
            }
            return std::nullopt;
        }
    }

    std::optional<Value> parseArray(int depth) {
        ++pos_;
        Array array;
        skipWhitespace();
        if (consume(']')) {
            return Value{std::move(array)};
        }
        while (true) {
            skipWhitespace();
            if (options_.lenient && consume(']')) {
                return Value{std::move(array)};
            }
            auto value = parseValue(depth + 1);
            if (!value) {
                return std::nullopt;
            }
            array.push_back(std::move(*value));
            skipWhitespace();
            if (consume(',')) {
                continue;
            }
            if (consume(']')) {
                return Value{std::move(array)};
            }
            return std::nullopt;
        }
    }

    std::optional<Value> parseNumber() {
        const std::size_t start = pos_;
        bool isInteger = true;
        if (pos_ < text_.size() && text_[pos_] == '-') {
            ++pos_;
        }
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c >= '0' && c <= '9') {
                ++pos_;
            } else if (c == '.' || c == 'e' || c == 'E' || c == '+' || c == '-') {
                isInteger = false;
                ++pos_;
            } else {
                break;
            }
        }
        const char* first = text_.data() + start;
        const char* last = text_.data() + pos_;
        if (first == last) {
            return std::nullopt;
        }
        if (isInteger) {
            std::int64_t value = 0;
            if (auto [ptr, ec] = std::from_chars(first, last, value); ec == std::errc{} && ptr == last) {
                return Value{value};
            }
        }
        std::string copy{first, last};
        char* end = nullptr;
        const double value = std::strtod(copy.c_str(), &end);
        if (end != copy.c_str() + copy.size()) {
            return std::nullopt;
        }
        return Value{value};
    }

    std::optional<std::string> parseString() {
        ++pos_;
        std::string out;
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"') {
                return out;
            }
            if (c != '\\') {
                out.push_back(c);
                continue;
            }
            if (pos_ >= text_.size()) {
                return std::nullopt;
            }
            switch (const char escape = text_[pos_++]) {
            case '"':
            case '\\':
            case '/':
                out.push_back(escape);
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
                if (!parseUnicodeEscape(out)) {
                    return std::nullopt;
                }
                break;
            default:
                return std::nullopt;
            }
        }
        return std::nullopt;
    }

    std::optional<std::uint32_t> parseHex4() {
        if (pos_ + 4 > text_.size()) {
            return std::nullopt;
        }
        std::uint32_t value = 0;
        auto [ptr, ec] = std::from_chars(text_.data() + pos_, text_.data() + pos_ + 4, value, 16);
        if (ec != std::errc{} || ptr != text_.data() + pos_ + 4) {
            return std::nullopt;
        }
        pos_ += 4;
        return value;
    }

    bool parseUnicodeEscape(std::string& out) {
        auto unit = parseHex4();
        if (!unit) {
            return false;
        }
        std::uint32_t codePoint = *unit;
        if (codePoint >= 0xD800 && codePoint <= 0xDBFF && text_.substr(pos_, 2) == "\\u") {
            pos_ += 2;
            auto low = parseHex4();
            if (!low || *low < 0xDC00 || *low > 0xDFFF) {
                return false;
            }
            codePoint = 0x10000 + ((codePoint - 0xD800) << 10) + (*low - 0xDC00);
        }
        appendUtf8(out, codePoint);
        return true;
    }

    static void appendUtf8(std::string& out, std::uint32_t cp) {
        if (cp < 0x80) {
            out.push_back(static_cast<char>(cp));
        } else if (cp < 0x800) {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else if (cp < 0x10000) {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool consume(char c) {
        if (pos_ < text_.size() && text_[pos_] == c) {
            ++pos_;
            return true;
        }
        return false;
    }

    void skipWhitespace() {
        while (pos_ < text_.size()) {
            const char c = text_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
            } else if (options_.lenient && c == '/' && pos_ + 1 < text_.size()) {
                if (text_[pos_ + 1] == '/') {
                    const auto end = text_.find('\n', pos_);
                    pos_ = end == std::string_view::npos ? text_.size() : end;
                } else if (text_[pos_ + 1] == '*') {
                    const auto end = text_.find("*/", pos_ + 2);
                    pos_ = end == std::string_view::npos ? text_.size() : end + 2;
                } else {
                    return;
                }
            } else {
                return;
            }
        }
    }

    std::string_view text_;
    ParseOptions options_;
    std::size_t pos_ = 0;
};

void writeString(std::string& out, std::string_view text) {
    constexpr char hex[] = "0123456789abcdef";
    out.push_back('"');
    for (char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        switch (c) {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20) {
                out += "\\u00";
                out.push_back(hex[c >> 4]);
                out.push_back(hex[c & 0x0F]);
            } else {
                out.push_back(ch);
            }
        }
    }
    out.push_back('"');
}

void write(std::string& out, const Value& value) {
    std::visit(
        [&out](const auto& data) {
            using T = std::decay_t<decltype(data)>;
            if constexpr (std::is_same_v<T, std::nullptr_t>) {
                out += "null";
            } else if constexpr (std::is_same_v<T, bool>) {
                out += data ? "true" : "false";
            } else if constexpr (std::is_same_v<T, std::int64_t>) {
                char buffer[24];
                auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), data);
                out.append(buffer, end);
            } else if constexpr (std::is_same_v<T, double>) {
                if (!std::isfinite(data)) {
                    out += "null";
                } else {
                    char buffer[32];
                    const int size = std::snprintf(buffer, sizeof(buffer), "%.17g", data);
                    out.append(buffer, static_cast<std::size_t>(size));
                }
            } else if constexpr (std::is_same_v<T, std::string>) {
                writeString(out, data);
            } else if constexpr (std::is_same_v<T, Array>) {
                out.push_back('[');
                bool first = true;
                for (const auto& item : data) {
                    if (!first) {
                        out.push_back(',');
                    }
                    first = false;
                    write(out, item);
                }
                out.push_back(']');
            } else {
                out.push_back('{');
                bool first = true;
                for (const auto& [key, item] : data) {
                    if (!first) {
                        out.push_back(',');
                    }
                    first = false;
                    writeString(out, key);
                    out.push_back(':');
                    write(out, item);
                }
                out.push_back('}');
            }
        },
        value.storage());
}

} // namespace

std::optional<Value> parse(std::string_view text, ParseOptions options) {
    return Parser{text, options}.parseDocument();
}

std::string serialize(const Value& value) {
    std::string out;
    write(out, value);
    return out;
}

} // namespace as3lsp::json
