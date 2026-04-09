include(FetchContent)
FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

function(copy_renderdoc_runtime_to_test target_name)
    if(RENDERDOC_DLL)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RENDERDOC_DLL}" "$<TARGET_FILE_DIR:${target_name}>")
    endif()

    if(RENDERDOC_JSON)
        add_custom_command(TARGET ${target_name} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RENDERDOC_JSON}" "$<TARGET_FILE_DIR:${target_name}>")
    endif()
endfunction()

# --- Unit tests (no renderdoc dependency) ---
# NOTE: test_serialization.cpp and session_stub.cpp will be added in Tasks 5-6
add_executable(test-unit
    unit/test_mcp_server.cpp
    unit/test_tool_registry.cpp
    unit/test_serialization.cpp
    unit/session_stub.cpp
    unit/test_pass_analysis.cpp
)
target_link_libraries(test-unit PRIVATE renderdoc-mcp-proto GTest::gtest_main)
gtest_discover_tests(test-unit PROPERTIES LABELS unit)

# --- CLI parse tests (no renderdoc dependency) ---
add_executable(test-cli-parse
    unit/test_cli_parse.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../src/cli/cli_parse.cpp
)
target_include_directories(test-cli-parse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(test-cli-parse PRIVATE GTest::gtest_main)
gtest_discover_tests(test-cli-parse PROPERTIES LABELS unit)

# --- Integration tests (require renderdoc) ---
if(TARGET renderdoc-mcp-lib)
    set(RENDERDOC_TEST_CAPTURE "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/vkcube.rdc"
        CACHE FILEPATH "Path to test .rdc capture file")

    file(GLOB TEST_TOOLS_SOURCES integration/test_tools*.cpp)
    if(TEST_TOOLS_SOURCES)
        add_executable(test-tools ${TEST_TOOLS_SOURCES})
        target_link_libraries(test-tools PRIVATE renderdoc-mcp-lib GTest::gtest_main)
        target_compile_definitions(test-tools PRIVATE TEST_RDC_PATH="${RENDERDOC_TEST_CAPTURE}")
        gtest_discover_tests(test-tools PROPERTIES LABELS integration DISCOVERY_MODE PRE_TEST)
        copy_renderdoc_runtime_to_test(test-tools)
    endif()

    add_executable(test-diff-algo unit/test_diff_algo.cpp)
    target_link_libraries(test-diff-algo PRIVATE renderdoc-core GTest::gtest_main)
    gtest_discover_tests(test-diff-algo PROPERTIES LABELS unit DISCOVERY_MODE PRE_TEST)
    copy_renderdoc_runtime_to_test(test-diff-algo)

    add_executable(test-capture integration/test_capture.cpp)
    target_link_libraries(test-capture PRIVATE renderdoc-core GTest::gtest_main)
    gtest_discover_tests(test-capture PROPERTIES LABELS integration DISCOVERY_MODE PRE_TEST)
    copy_renderdoc_runtime_to_test(test-capture)

    file(GLOB TEST_INTEGRATION_SOURCES integration/test_protocol*.cpp integration/test_workflow*.cpp)
    if(TEST_INTEGRATION_SOURCES)
        add_executable(test-integration ${TEST_INTEGRATION_SOURCES})
        target_link_libraries(test-integration PRIVATE GTest::gtest_main nlohmann_json::nlohmann_json)
        target_include_directories(test-integration PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
        target_compile_definitions(test-integration PRIVATE
            TEST_EXE_PATH="$<TARGET_FILE:renderdoc-mcp>"
            TEST_RDC_PATH="${RENDERDOC_TEST_CAPTURE}")
        gtest_discover_tests(test-integration PROPERTIES LABELS integration DISCOVERY_MODE PRE_TEST)
    endif()
endif()
