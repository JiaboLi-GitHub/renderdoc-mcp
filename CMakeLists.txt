cmake_minimum_required(VERSION 3.16)
project(renderdoc-mcp LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(BUILD_TESTING "Build tests" OFF)

set(RENDERDOC_DIR "" CACHE PATH "Path to RenderDoc source tree")
set(RENDERDOC_BUILD_DIR "" CACHE PATH "Path to RenderDoc build output")

# --- nlohmann/json ---
include(FetchContent)
FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
FetchContent_MakeAvailable(json)

# --- renderdoc-mcp-proto (always built, NO renderdoc dependency) ---
add_library(renderdoc-mcp-proto STATIC
    src/mcp/mcp_server.cpp
    src/mcp/tool_registry.cpp
    src/mcp/serialization.cpp
)
target_include_directories(renderdoc-mcp-proto PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(renderdoc-mcp-proto PUBLIC nlohmann_json::nlohmann_json)

# --- renderdoc-core and full targets (require RENDERDOC_DIR) ---
if(RENDERDOC_DIR)
    add_library(renderdoc-core STATIC
        src/core/errors.cpp
        src/core/session.cpp
        src/core/events.cpp
        src/core/info.cpp
        src/core/resources.cpp
        src/core/shaders.cpp
        src/core/pipeline.cpp
        src/core/export.cpp
        src/core/capture.cpp
        src/core/renderdoc_tostr_anchor.cpp
        src/core/pixel.cpp
        src/core/debug.cpp
        src/core/texstats.cpp
        src/core/shader_edit.cpp
        src/core/mesh.cpp
        src/core/snapshot.cpp
        src/core/usage.cpp
    )
    target_include_directories(renderdoc-core PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party
        ${RENDERDOC_DIR}/renderdoc/api/replay
        ${RENDERDOC_DIR}/renderdoc/api
    )

    if(WIN32)
        target_compile_definitions(renderdoc-core PUBLIC RENDERDOC_PLATFORM_WIN32)
    elseif(APPLE)
        target_compile_definitions(renderdoc-core PUBLIC RENDERDOC_PLATFORM_APPLE)
    else()
        target_compile_definitions(renderdoc-core PUBLIC RENDERDOC_PLATFORM_LINUX)
    endif()

    if(RENDERDOC_BUILD_DIR)
        find_library(RENDERDOC_LIB renderdoc
            PATHS "${RENDERDOC_BUILD_DIR}/lib/Release"
                  "${RENDERDOC_BUILD_DIR}/lib/Development"
                  "${RENDERDOC_BUILD_DIR}/lib"
                  "${RENDERDOC_BUILD_DIR}/Development"
                  "${RENDERDOC_BUILD_DIR}/Release"
                  "${RENDERDOC_BUILD_DIR}"
            NO_DEFAULT_PATH
        )
    else()
        find_library(RENDERDOC_LIB renderdoc
            PATHS "${RENDERDOC_DIR}/build/lib/Release"
                  "${RENDERDOC_DIR}/build/lib/Development"
                  "${RENDERDOC_DIR}/build/lib"
                  "${RENDERDOC_DIR}/x64/Development"
                  "${RENDERDOC_DIR}/x64/Release"
                  "${RENDERDOC_DIR}/Win32/Release"
            NO_DEFAULT_PATH
        )
    endif()

    if(RENDERDOC_LIB)
        target_link_libraries(renderdoc-core PUBLIC ${RENDERDOC_LIB})
        message(STATUS "Found renderdoc library: ${RENDERDOC_LIB}")
    else()
        message(WARNING "renderdoc.lib not found; renderdoc-core will not link")
        target_link_libraries(renderdoc-core PUBLIC renderdoc)
    endif()

    # renderdoc-mcp-lib (tools wiring core -> MCP, plus default constructor)
    add_library(renderdoc-mcp-lib STATIC
        src/mcp/mcp_server_default.cpp
        src/mcp/action_flags.cpp
        src/mcp/tools/session_tools.cpp
        src/mcp/tools/event_tools.cpp
        src/mcp/tools/info_tools.cpp
        src/mcp/tools/resource_tools.cpp
        src/mcp/tools/shader_tools.cpp
        src/mcp/tools/pipeline_tools.cpp
        src/mcp/tools/export_tools.cpp
        src/mcp/tools/capture_tools.cpp
        src/mcp/tools/pixel_tools.cpp
        src/mcp/tools/debug_tools.cpp
        src/mcp/tools/texstats_tools.cpp
    )
    target_link_libraries(renderdoc-mcp-lib PUBLIC renderdoc-mcp-proto renderdoc-core)

    # renderdoc-mcp executable
    add_executable(renderdoc-mcp src/main.cpp)
    target_link_libraries(renderdoc-mcp PRIVATE renderdoc-mcp-lib)

    # renderdoc-cli — one-shot compound-command CLI
    add_executable(renderdoc-cli src/cli/main.cpp src/core/replay_marker.cpp)
    target_link_libraries(renderdoc-cli PRIVATE renderdoc-core)

    # Locate RenderDoc runtime files for local execution and packaging.
    if(RENDERDOC_BUILD_DIR)
        set(_RUNTIME_SEARCH "${RENDERDOC_BUILD_DIR}/bin/Release" "${RENDERDOC_BUILD_DIR}/bin/Development" "${RENDERDOC_BUILD_DIR}/bin" "${RENDERDOC_BUILD_DIR}/Development" "${RENDERDOC_BUILD_DIR}/Release" "${RENDERDOC_BUILD_DIR}")
    else()
        set(_RUNTIME_SEARCH "${RENDERDOC_DIR}/build/bin/Release" "${RENDERDOC_DIR}/build/bin/Development" "${RENDERDOC_DIR}/build/bin" "${RENDERDOC_DIR}/x64/Development" "${RENDERDOC_DIR}/x64/Release")
    endif()
    find_file(RENDERDOC_DLL renderdoc.dll PATHS ${_RUNTIME_SEARCH} NO_DEFAULT_PATH)
    find_file(RENDERDOC_JSON renderdoc.json PATHS ${_RUNTIME_SEARCH} NO_DEFAULT_PATH)

    function(copy_renderdoc_runtime target_name)
        if(RENDERDOC_DLL)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RENDERDOC_DLL}" "$<TARGET_FILE_DIR:${target_name}>"
                COMMENT "Copying renderdoc.dll to output directory")
        endif()

        if(RENDERDOC_JSON)
            add_custom_command(TARGET ${target_name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RENDERDOC_JSON}" "$<TARGET_FILE_DIR:${target_name}>"
                COMMENT "Copying renderdoc.json to output directory")
        endif()
    endfunction()

    copy_renderdoc_runtime(renderdoc-mcp)
    copy_renderdoc_runtime(renderdoc-cli)

    # --- Install rules ---
    install(TARGETS renderdoc-mcp renderdoc-cli RUNTIME DESTINATION bin)
    if(RENDERDOC_DLL)
        install(FILES "${RENDERDOC_DLL}" DESTINATION bin)
    endif()
    if(RENDERDOC_JSON)
        install(FILES "${RENDERDOC_JSON}" DESTINATION bin)
    endif()
    install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/skills/ DESTINATION skills)
else()
    message(STATUS "RENDERDOC_DIR not set - building renderdoc-mcp-proto only.")
endif()

if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
