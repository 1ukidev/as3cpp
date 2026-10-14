#pragma once

#include <concepts>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace as3lsp::json {

class Value;
using Array = std::vector<Value>;

// Insertion-ordered object. LSP objects are small, so a linear search is faster than hashing.
class Object {
public:
    using Entry = std::pair<std::string, Value>;

    Object();
    Object(std::initializer_list<Entry> entries);
    Object(const Object&);
    Object(Object&&) noexcept;
    Object& operator=(const Object&);
    Object& operator=(Object&&) noexcept;
    ~Object();

    [[nodiscard]] const Value* find(std::string_view key) const;
    [[nodiscard]] Value* find(std::string_view key);
    Value& operator[](std::string_view key);

    [[nodiscard]] std::size_t size() const;
    [[nodiscard]] bool empty() const;
    [[nodiscard]] std::vector<Entry>::const_iterator begin() const;
    [[nodiscard]] std::vector<Entry>::const_iterator end() const;

private:
    std::vector<Entry> entries_;
};

class Value {
public:
    using Storage = std::variant<std::nullptr_t, bool, std::int64_t, double, std::string, Array, Object>;

    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool value) : data_(value) {}
    template <std::integral T>
        requires(!std::same_as<T, bool>)
    Value(T value) : data_(static_cast<std::int64_t>(value)) {}
    Value(double value) : data_(value) {}
    Value(const char* value) : data_(std::string{value}) {}
    Value(std::string value) : data_(std::move(value)) {}
    Value(std::string_view value) : data_(std::string{value}) {}
    Value(Array value) : data_(std::move(value)) {}
    Value(Object value) : data_(std::move(value)) {}

    [[nodiscard]] bool isNull() const { return std::holds_alternative<std::nullptr_t>(data_); }
    [[nodiscard]] bool isBool() const { return std::holds_alternative<bool>(data_); }
    [[nodiscard]] bool isInteger() const { return std::holds_alternative<std::int64_t>(data_); }
    [[nodiscard]] bool isNumber() const { return isInteger() || std::holds_alternative<double>(data_); }
    [[nodiscard]] bool isString() const { return std::holds_alternative<std::string>(data_); }
    [[nodiscard]] bool isArray() const { return std::holds_alternative<Array>(data_); }
    [[nodiscard]] bool isObject() const { return std::holds_alternative<Object>(data_); }

    [[nodiscard]] bool asBool(bool fallback = false) const;
    [[nodiscard]] std::int64_t asInt(std::int64_t fallback = 0) const;
    [[nodiscard]] double asDouble(double fallback = 0.0) const;
    [[nodiscard]] std::string_view asString(std::string_view fallback = {}) const;

    // These accessors throw std::bad_variant_access when the value has another type.
    [[nodiscard]] const Array& array() const { return std::get<Array>(data_); }
    [[nodiscard]] Array& array() { return std::get<Array>(data_); }
    [[nodiscard]] const Object& object() const { return std::get<Object>(data_); }
    [[nodiscard]] Object& object() { return std::get<Object>(data_); }

    // Returns nullptr when this is not an object or the key is missing.
    [[nodiscard]] const Value* find(std::string_view key) const;
    // Returns a null value when this is not an object or the key is missing.
    [[nodiscard]] const Value& operator[](std::string_view key) const;
    // Converts a null value to an object, then inserts the key when it is missing.
    Value& operator[](std::string_view key);

    void push(Value value);

    [[nodiscard]] const Storage& storage() const { return data_; }

    bool operator==(const Value&) const;

private:
    Storage data_;
};

inline Object::Object() = default;
inline Object::Object(std::initializer_list<Entry> entries) : entries_(entries) {}
inline Object::Object(const Object&) = default;
inline Object::Object(Object&&) noexcept = default;
inline Object& Object::operator=(const Object&) = default;
inline Object& Object::operator=(Object&&) noexcept = default;
inline Object::~Object() = default;
inline std::size_t Object::size() const {
    return entries_.size();
}
inline bool Object::empty() const {
    return entries_.empty();
}
inline std::vector<Object::Entry>::const_iterator Object::begin() const {
    return entries_.begin();
}
inline std::vector<Object::Entry>::const_iterator Object::end() const {
    return entries_.end();
}

struct ParseOptions {
    // Accepts comments and trailing commas, as found in configuration files such as asconfig.json.
    bool lenient = false;
};

[[nodiscard]] std::optional<Value> parse(std::string_view text, ParseOptions options = {});
[[nodiscard]] std::string serialize(const Value& value);

} // namespace as3lsp::json
