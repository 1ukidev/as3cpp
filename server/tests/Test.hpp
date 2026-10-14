#pragma once

#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

namespace as3lsp::test {

struct TestCase {
    const char* name;
    void (*run)();
};

std::vector<TestCase>& registry();

struct Registrar {
    Registrar(const char* name, void (*run)()) { registry().push_back({name, run}); }
};

struct Failure {
    std::string message;
};

[[noreturn]] void fail(const char* file, int line, const std::string& message);

template <typename T> decltype(auto) printable(const T& value) {
    if constexpr (std::is_enum_v<T>) {
        return static_cast<long long>(value);
    } else {
        return (value);
    }
}

} // namespace as3lsp::test

#define TEST_CASE(name)                                                                                      \
    static void name();                                                                                      \
    static const ::as3lsp::test::Registrar name##Registrar{#name, &name};                                    \
    static void name()

#define CHECK(expression)                                                                                    \
    do {                                                                                                     \
        if (!(expression)) {                                                                                 \
            ::as3lsp::test::fail(__FILE__, __LINE__, #expression);                                           \
        }                                                                                                    \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                           \
    do {                                                                                                     \
        const auto& actualValue = (actual);                                                                  \
        const auto& expectedValue = (expected);                                                              \
        if (!(actualValue == expectedValue)) {                                                               \
            std::ostringstream message;                                                                      \
            message << #actual << " == " << #expected                                                        \
                    << "\n    actual:   " << ::as3lsp::test::printable(actualValue)                          \
                    << "\n    expected: " << ::as3lsp::test::printable(expectedValue);                       \
            ::as3lsp::test::fail(__FILE__, __LINE__, message.str());                                         \
        }                                                                                                    \
    } while (false)
