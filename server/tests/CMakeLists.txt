add_executable(as3core_tests
    TestMain.cpp
    AnalyzerTests.cpp
    AsConfigTests.cpp
    DiagnosticsTests.cpp
    JsonTests.cpp
    LexerTests.cpp
    LibraryTests.cpp
    MxmlTests.cpp
    ParserTests.cpp
    SourceLocatorTests.cpp
)
target_link_libraries(as3core_tests PRIVATE as3core)

add_test(NAME as3core_tests COMMAND as3core_tests)

find_program(NODE_EXECUTABLE node)
if(NODE_EXECUTABLE)
    add_test(NAME lsp_smoke
        COMMAND ${NODE_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/smoke.mjs $<TARGET_FILE:as3lsp> ${PROJECT_SOURCE_DIR}/samples/demo)
endif()
