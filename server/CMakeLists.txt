find_package(Threads REQUIRED)

add_library(as3core STATIC
    src/abc/AbcReader.cpp
    src/abc/Inflate.cpp
    src/abc/Library.cpp
    src/abc/Zip.cpp
    src/analysis/Analyzer.cpp
    src/analysis/Completion.cpp
    src/analysis/Document.cpp
    src/analysis/Expression.cpp
    src/analysis/LineIndex.cpp
    src/analysis/Resolver.cpp
    src/build/AsConfig.cpp
    src/build/Compiler.cpp
    src/build/Process.cpp
    src/index/Index.cpp
    src/index/Project.cpp
    src/index/SourceLocator.cpp
    src/json/Json.cpp
    src/lsp/Protocol.cpp
    src/lsp/Server.cpp
    src/lsp/Transport.cpp
    src/model/Model.cpp
    src/syntax/Lexer.cpp
    src/syntax/Mxml.cpp
    src/syntax/Parser.cpp
    src/util/Files.cpp
    src/util/Strings.cpp
    src/util/Uri.cpp
)

target_include_directories(as3core PUBLIC src)
target_link_libraries(as3core PUBLIC Threads::Threads)
target_compile_definitions(as3core PUBLIC AS3LSP_VERSION="${PROJECT_VERSION}")

if(MSVC)
    target_compile_options(as3core PUBLIC /W4 /permissive- /utf-8)
else()
    target_compile_options(as3core PUBLIC -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
endif()

add_executable(as3lsp src/main.cpp)
target_link_libraries(as3lsp PRIVATE as3core)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_options(as3lsp PRIVATE -static-libstdc++ -static-libgcc)
elseif(MINGW)
    target_link_options(as3lsp PRIVATE -static $<$<CONFIG:Release>:-s>)
endif()

if(AS3LSP_BUILD_TESTS)
    add_subdirectory(tests)
endif()
