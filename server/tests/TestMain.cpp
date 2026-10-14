#include "Test.hpp"

#include <cstdio>
#include <exception>
#include <string_view>

namespace as3lsp::test {

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

void fail(const char* file, int line, const std::string& message) {
    throw Failure{std::string{file} + ":" + std::to_string(line) + ": " + message};
}

} // namespace as3lsp::test

int main(int argc, char** argv) {
    const std::string_view filter = argc > 1 ? argv[1] : "";
    int failed = 0;
    int passed = 0;
    for (const auto& test : as3lsp::test::registry()) {
        if (!filter.empty() && std::string_view{test.name}.find(filter) == std::string_view::npos) {
            continue;
        }
        try {
            test.run();
            ++passed;
        } catch (const as3lsp::test::Failure& failure) {
            ++failed;
            std::printf("FAIL %s\n  %s\n", test.name, failure.message.c_str());
        } catch (const std::exception& error) {
            ++failed;
            std::printf("FAIL %s\n  exception: %s\n", test.name, error.what());
        }
    }
    std::printf("%d passed, %d failed\n", passed, failed);
    return failed == 0 ? 0 : 1;
}
