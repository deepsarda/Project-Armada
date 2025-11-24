cmake_minimum_required(VERSION 3.20)
project(armada LANGUAGES C CXX)

# Compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_compile_options(-Wall -Wextra)

# Include local headers
include_directories(include)

include(FetchContent)
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/ftxui
    GIT_TAG v6.1.9
)
FetchContent_MakeAvailable(ftxui)

set(SRC_DIR src)

file(GLOB_RECURSE C_SRCS
    "${SRC_DIR}/networking/*.c"
    "${SRC_DIR}/client/*.c"
    "${SRC_DIR}/server/*.c"
)

file(GLOB_RECURSE CPP_SRCS
    "${SRC_DIR}/*.cpp"
    "${SRC_DIR}/ui/*.cpp"
)

add_executable(armada ${C_SRCS} ${CPP_SRCS})

target_link_libraries(armada PRIVATE
    ftxui::component
    ftxui::dom
    ftxui::screen
    pthread
)

include(GNUInstallDirs)

install(TARGETS armada
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)
