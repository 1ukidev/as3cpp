cmake_minimum_required(VERSION 3.25)

project(as3cpp VERSION 0.1.0 DESCRIPTION "Fast ActionScript 3 language server" LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

option(AS3LSP_BUILD_TESTS "Build the unit tests" ON)

if(AS3LSP_BUILD_TESTS)
    enable_testing()
endif()

add_subdirectory(server)
