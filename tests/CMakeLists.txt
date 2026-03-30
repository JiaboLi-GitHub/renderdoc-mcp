include(FetchContent)
FetchContent_Declare(googletest URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

include(GoogleTest)

# --- Unit tests (no renderdoc dependency) ---
# NOTE: test_serialization.cpp and session_stub.cpp will be added in Tasks 5-6
add_executable(test-unit
    unit/test_mcp_server.cpp
    unit/test_tool_registry.cpp
    unit/test_serialization.cpp
    unit/renderdoc_wrapper_stub.cpp
)
target_link_libraries(test-unit PRIVATE renderdoc-mcp-proto GTest::gtest_main)
gtest_discover_tests(test-unit PROPERTIES LABELS unit)

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
        if(RENDERDOC_DLL)
            add_custom_command(TARGET test-tools POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RENDERDOC_DLL}" "$<TARGET_FILE_DIR:test-tools>")
        endif()
    endif()

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
