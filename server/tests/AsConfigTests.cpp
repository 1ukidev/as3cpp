#include "Test.hpp"
#include "build/AsConfig.hpp"

#include <filesystem>
#include <fstream>

using namespace as3lsp;

namespace {

build::BuildPlan plan(std::string_view text, build::BuildOptions options = {},
                      const std::filesystem::path& directory = ".") {
    const auto config = json::parse(text, {.lenient = true});
    CHECK(config.has_value());
    return build::planBuild(*config, directory, options);
}

std::string joined(const std::vector<std::string>& arguments) {
    std::string text;
    for (const auto& argument : arguments) {
        text += argument + "\n";
    }
    return text;
}

} // namespace

TEST_CASE(asconfigBuildsApplicationArguments) {
    const auto result = plan(R"({
        // Comments are allowed, as in vscode-as3mxml.
        "config": "air",
        "compilerOptions": {
            "output": "bin-debug/smt_venda.swf",
            "accessible": true,
            "debug": true,
            "source-path": ["src", "../smt_lib/assets"],
            "library-path": ["libs", "../smt_lib/bin/smt_lib.swc"],
            "define": [{"name": "CONFIG::debug", "value": true}],
            "locale": ["en_US", "pt_BR"],
        },
        "files": ["src/smt_venda.mxml"],
        "application": "src/smt_venda-app.xml",
        "copySourcePathAssets": true,
        "additionalOptions": "-locale en_US -services \"src/services config.xml\""
    })",
                             {.debug = false});
    CHECK(!result.isLibrary());
    CHECK_EQ(joined(result.arguments), "--debug=false\n"
                                       "+configname=air\n"
                                       "--output=bin-debug/smt_venda.swf\n"
                                       "--accessible=true\n"
                                       "--source-path+=src\n"
                                       "--source-path+=../smt_lib/assets\n"
                                       "--library-path+=libs\n"
                                       "--library-path+=../smt_lib/bin/smt_lib.swc\n"
                                       "--define+=CONFIG::debug,true\n"
                                       "--locale=en_US\n"
                                       "--locale+=pt_BR\n"
                                       "-locale\n"
                                       "en_US\n"
                                       "-services\n"
                                       "src/services config.xml\n"
                                       "--\n"
                                       "src/smt_venda.mxml\n");
    CHECK_EQ(result.mainFile, "src/smt_venda.mxml");
    CHECK_EQ(result.applicationDescriptors.size(), std::size_t{1});
    CHECK(result.copySourcePathAssets);
    CHECK_EQ(result.outputDirectory("/work/smt_venda").generic_string(), "/work/smt_venda/bin-debug");
}

TEST_CASE(asconfigBuildsLibraryArguments) {
    const auto result = plan(R"({
        "config": "air",
        "type": "lib",
        "compilerOptions": {
            "output": "bin/smt_lib.swc",
            "load-config": ["compile-config-vs.xml"],
            "include-sources": ["src"],
            "include-file": [{"file": "assets/a.png", "path": "br/a.png"}, "b.css"]
        },
        "files": ["src/Extra.as"]
    })");
    CHECK(result.isLibrary());
    CHECK_EQ(joined(result.arguments), "+configname=air\n"
                                       "--output=bin/smt_lib.swc\n"
                                       "--load-config+=compile-config-vs.xml\n"
                                       "--include-sources+=src\n"
                                       "--include-file+=br/a.png,assets/a.png\n"
                                       "--include-file+=b.css,b.css\n"
                                       "--include-sources+=src/Extra.as\n");
    CHECK(result.mainFile.empty());
}

TEST_CASE(asconfigResolvesMainClass) {
    const auto directory = std::filesystem::temp_directory_path() / "as3cpp-asconfig-test";
    std::filesystem::create_directories(directory / "src" / "app");
    std::ofstream{directory / "src" / "app" / "Main.as"} << "package app { public class Main {} }";
    const auto result =
        plan(R"({"compilerOptions": {"source-path": ["src"]}, "mainClass": "app.Main"})", {}, directory);
    CHECK_EQ(result.mainFile, "src/app/Main.as");
    CHECK_EQ(result.arguments.back(), "src/app/Main.as");
    std::filesystem::remove_all(directory);
}

TEST_CASE(asconfigRejectsUnknownObjectOptions) {
    bool failed = false;
    try {
        (void)plan(R"({"compilerOptions": {"unknown": {"a": 1}}})");
    } catch (const build::BuildError&) {
        failed = true;
    }
    CHECK(failed);
}

TEST_CASE(asconfigSplitsAdditionalOptions) {
    const auto arguments = build::splitArguments("  -a  'b c'\t\"d\"e -f=\"g h\" ");
    CHECK_EQ(joined(arguments), "-a\nb c\nde\n-f=g h\n");
}
