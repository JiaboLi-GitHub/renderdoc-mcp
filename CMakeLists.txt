cmake_minimum_required(VERSION 3.16)
project(renderdoc-mcp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── options ─────────────────────────────────────────────────────────────────
option(BUILD_TESTING "Build test targets" OFF)

# ── renderdoc paths ─────────────────────────────────────────────────────────
set(RENDERDOC_DIR "" CACHE PATH "Path to renderdoc source root (contains renderdoc/api/replay/)")
set(RENDERDOC_BUILD_DIR "" CACHE PATH "Path to renderdoc CMake build output (contains lib/Release/renderdoc.lib)")

# ── nlohmann/json via FetchContent ──────────────────────────────────────────
include(FetchContent)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# ── renderdoc-mcp-core (static library, always built) ──────────────────────
add_library(renderdoc-mcp-core STATIC
    src/mcp_server.cpp
    src/tool_registry.cpp
)

target_include_directories(renderdoc-mcp-core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_link_libraries(renderdoc-mcp-core PUBLIC nlohmann_json::nlohmann_json)

# ── renderdoc-dependent targets (only when RENDERDOC_DIR is set) ────────────
if(RENDERDOC_DIR)
    # ── renderdoc-mcp-lib (static library) ──────────────────────────────────
    add_library(renderdoc-mcp-lib STATIC
        src/mcp_server_default.cpp
        src/renderdoc_wrapper.cpp
        src/tools/session_tools.cpp
        src/tools/event_tools.cpp
        src/tools/pipeline_tools.cpp
        src/tools/export_tools.cpp
        src/tools/info_tools.cpp
        src/tools/resource_tools.cpp
        src/tools/shader_tools.cpp
    )

    target_link_libraries(renderdoc-mcp-lib PUBLIC renderdoc-mcp-core)

    # ── platform defines (required by renderdoc headers) ────────────────────
    if(WIN32)
        target_compile_definitions(renderdoc-mcp-lib PUBLIC RENDERDOC_PLATFORM_WIN32)
    elseif(APPLE)
        target_compile_definitions(renderdoc-mcp-lib PUBLIC RENDERDOC_PLATFORM_APPLE)
    elseif(UNIX)
        target_compile_definitions(renderdoc-mcp-lib PUBLIC RENDERDOC_PLATFORM_LINUX)
    endif()

    # ── renderdoc include paths ─────────────────────────────────────────────
    target_include_directories(renderdoc-mcp-lib PUBLIC
        ${RENDERDOC_DIR}/renderdoc/api/replay
        ${RENDERDOC_DIR}/renderdoc/api
    )

    # ── link renderdoc ──────────────────────────────────────────────────────
    if(RENDERDOC_BUILD_DIR)
        # User provided explicit build dir
        find_library(RENDERDOC_LIB renderdoc
            PATHS "${RENDERDOC_BUILD_DIR}/lib/Release"
                  "${RENDERDOC_BUILD_DIR}/lib"
                  "${RENDERDOC_BUILD_DIR}/Release"
                  "${RENDERDOC_BUILD_DIR}"
            NO_DEFAULT_PATH
        )
    else()
        # Try common build output locations relative to RENDERDOC_DIR
        find_library(RENDERDOC_LIB renderdoc
            PATHS "${RENDERDOC_DIR}/build/lib/Release"
                  "${RENDERDOC_DIR}/build/lib"
                  "${RENDERDOC_DIR}/x64/Release"
                  "${RENDERDOC_DIR}/Win32/Release"
            NO_DEFAULT_PATH
        )
    endif()

    if(RENDERDOC_LIB)
        target_link_libraries(renderdoc-mcp-lib PUBLIC ${RENDERDOC_LIB})
        message(STATUS "Found renderdoc library: ${RENDERDOC_LIB}")
    else()
        message(WARNING "renderdoc.lib not found - will attempt link anyway. Set RENDERDOC_BUILD_DIR if build fails.")
        target_link_libraries(renderdoc-mcp-lib PUBLIC renderdoc)
    endif()

    # ── renderdoc-mcp (executable) ──────────────────────────────────────────
    add_executable(renderdoc-mcp src/main.cpp)
    target_link_libraries(renderdoc-mcp PRIVATE renderdoc-mcp-lib)

    # ── copy renderdoc.dll next to executable (post-build) ──────────────────
    if(RENDERDOC_BUILD_DIR)
        set(RENDERDOC_DLL_SEARCH_DIRS
            "${RENDERDOC_BUILD_DIR}/bin/Release"
            "${RENDERDOC_BUILD_DIR}/bin"
            "${RENDERDOC_BUILD_DIR}/Release"
            "${RENDERDOC_BUILD_DIR}"
        )
    else()
        set(RENDERDOC_DLL_SEARCH_DIRS
            "${RENDERDOC_DIR}/build/bin/Release"
            "${RENDERDOC_DIR}/build/bin"
            "${RENDERDOC_DIR}/x64/Release"
        )
    endif()

    find_file(RENDERDOC_DLL renderdoc.dll PATHS ${RENDERDOC_DLL_SEARCH_DIRS} NO_DEFAULT_PATH)
    if(RENDERDOC_DLL)
        add_custom_command(TARGET renderdoc-mcp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${RENDERDOC_DLL}" "$<TARGET_FILE_DIR:renderdoc-mcp>"
            COMMENT "Copying renderdoc.dll to output directory"
        )
        message(STATUS "Found renderdoc.dll: ${RENDERDOC_DLL}")
    else()
        message(WARNING "renderdoc.dll not found - you must place it next to the executable manually.")
    endif()
else()
    message(STATUS "RENDERDOC_DIR not set - building renderdoc-mcp-core only. Set RENDERDOC_DIR to build the full executable.")
endif()

# ── tests ───────────────────────────────────────────────────────────────────
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()
