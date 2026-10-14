#include "build/Compiler.hpp"
#include "lsp/Server.hpp"
#include "lsp/Transport.hpp"

#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

namespace {

constexpr const char* usage = R"(Usage:
  as3lsp                 Starts the language server on stdin and stdout.
  as3lsp compile [options]
                         Compiles the project of an asconfig.json file.
  as3lsp --version       Prints the version.

Compile options:
  --project <path>       The asconfig.json file, or its directory. Default: the current directory.
  --sdk <path>           An SDK with the Flex compiler (lib/mxmlc.jar). Required.
  --debug=<true|false>   Overrides compilerOptions.debug.
  --clean                Deletes the contents of the output directory instead of compiling.
  --java <path>          The Java executable. Default: $JAVA_HOME/bin/java, then java on the PATH.
  --jvmarg=<argument>    Adds an argument for the Java virtual machine. Can be repeated.
)";

// Reads "--name value" or "--name=value". Returns nullopt when the argument has another name.
std::optional<std::string> optionValue(int argc, char** argv, int& index, std::string_view name) {
    const std::string_view argument{argv[index]};
    if (!argument.starts_with(name)) {
        return std::nullopt;
    }
    const std::string_view rest = argument.substr(name.size());
    if (rest.starts_with('=')) {
        return std::string{rest.substr(1)};
    }
    if (rest.empty() && index + 1 < argc) {
        return std::string{argv[++index]};
    }
    return std::nullopt;
}

int runCompile(int argc, char** argv) {
    as3lsp::build::CompileOptions options;
    options.project = ".";
    for (int i = 2; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--clean" || argument == "--clean=true") {
            options.clean = true;
        } else if (auto project = optionValue(argc, argv, i, "--project")) {
            options.project = *project;
        } else if (auto sdk = optionValue(argc, argv, i, "--sdk")) {
            options.sdk = *sdk;
        } else if (argument == "--debug") {
            options.debug = true;
        } else if (auto debug = optionValue(argc, argv, i, "--debug")) {
            options.debug = *debug != "false";
        } else if (auto java = optionValue(argc, argv, i, "--java")) {
            options.java = *java;
        } else if (argument.starts_with("--jvmarg=")) {
            options.jvmArguments.emplace_back(argument.substr(9));
        } else {
            std::fprintf(stderr, "Unknown option: %s\n\n%s", argv[i], usage);
            return 2;
        }
    }
    if (options.sdk.empty() && !options.clean) {
        std::fprintf(stderr, "Error: no SDK with the Flex compiler was given with --sdk. "
                             "In VS Code, set \"as3cpp.compiler.sdkPath\".\n");
        return 2;
    }
    return as3lsp::build::compile(options);
}

} // namespace

int main(int argc, char** argv) {
    if (argc > 1) {
        const std::string_view command{argv[1]};
        if (command == "--version" || command == "-v") {
            std::printf("as3lsp %s\n", AS3LSP_VERSION);
            return 0;
        }
        if (command == "--help" || command == "-h") {
            std::printf("%s", usage);
            return 0;
        }
        if (command == "compile") {
            return runCompile(argc, argv);
        }
    }
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    as3lsp::lsp::Transport transport{stdin, stdout};
    as3lsp::lsp::Server server{transport};
    const int code = server.run();
    std::fflush(stdout);
    // The reader thread may still be blocked on stdin, so skip static destructors and thread joins.
    std::_Exit(code);
}
