cmake_minimum_required(VERSION 3.20)
project(armada LANGUAGES C CXX)

# ============================================================================
# Build Options
# ============================================================================
option(STATIC_BUILD "Build with static linking for standalone distribution" OFF)

# ============================================================================
# Compiler Settings
# ============================================================================
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Platform-specific compiler flags
if(MSVC)
    add_compile_options(/W4)
    if(STATIC_BUILD)
        # Use static runtime on Windows for standalone builds
        set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    endif()
else()
    add_compile_options(-Wall -Wextra)
    if(STATIC_BUILD)
        if(APPLE)
            # macOS doesn't support fully static linking, but we can link C++ statically
            add_link_options(-static-libstdc++)
        elseif(UNIX)
            # Linux: static linking
            add_link_options(-static-libgcc -static-libstdc++)
            # For fully static build on Linux (optional, may cause glibc issues)
            # add_link_options(-static)
        endif()
    endif()
endif()

# ============================================================================
# Include Headers
# ============================================================================
include_directories(include)

# ============================================================================
# Dependencies (fetched automatically - no user installation needed)
# ============================================================================
include(FetchContent)

# FTXUI - Terminal UI library
set(FTXUI_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(FTXUI_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(FTXUI_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
    GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)

# ============================================================================
# Source Files
# ============================================================================
set(SRC_DIR src)

file(GLOB_RECURSE C_SRCS
    "${SRC_DIR}/networking/*.c"
    "${SRC_DIR}/server/*.c"
)

file(GLOB_RECURSE CPP_SRCS
    "${SRC_DIR}/*.cpp"
    "${SRC_DIR}/client/*.cpp"
)

# ============================================================================
# Executable
# ============================================================================
add_executable(armada ${C_SRCS} ${CPP_SRCS})

# Platform-specific libraries
if(WIN32)
    target_link_libraries(armada PRIVATE
        ftxui::component
        ftxui::dom
        ftxui::screen
        ws2_32
    )
elseif(APPLE)
    target_link_libraries(armada PRIVATE
        ftxui::component
        ftxui::dom
        ftxui::screen
        pthread
    )
else()
    # Linux
    target_link_libraries(armada PRIVATE
        ftxui::component
        ftxui::dom
        ftxui::screen
        pthread
    )
endif()

# ============================================================================
# Installation
# ============================================================================
include(GNUInstallDirs)

install(TARGETS armada
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# ============================================================================
# CPack Configuration for Distribution Packages
# ============================================================================
set(CPACK_PACKAGE_NAME "armada")
set(CPACK_PACKAGE_VERSION "1.0.0")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Armada - A terminal-based application")
set(CPACK_PACKAGE_VENDOR "Armada Project")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/LICENSE")

if(WIN32)
    set(CPACK_GENERATOR "ZIP")
elseif(APPLE)
    set(CPACK_GENERATOR "ZIP")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
