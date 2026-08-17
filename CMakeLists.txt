cmake_minimum_required(VERSION 3.25)

project(MX_Central
    VERSION 1.0.0
    DESCRIPTION "MX Central XIM Matrix Config Finder"
    LANGUAGES CXX
)

# ---------------------------------------------------------
# C++ configuration
# ---------------------------------------------------------

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ---------------------------------------------------------
# Dependencies
# ---------------------------------------------------------

find_package(dpp CONFIG REQUIRED)
find_package(SQLite3 REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(spdlog CONFIG REQUIRED)

# ---------------------------------------------------------
# Source files
# ---------------------------------------------------------

file(GLOB_RECURSE MX_SOURCES
    CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp"
)

# ---------------------------------------------------------
# Executable
# ---------------------------------------------------------

add_executable(MX_Central
    ${MX_SOURCES}
)

# ---------------------------------------------------------
# Project include directories
# ---------------------------------------------------------

target_include_directories(MX_Central
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

# ---------------------------------------------------------
# Libraries
# ---------------------------------------------------------

target_link_libraries(MX_Central
    PRIVATE
        dpp::dpp
        SQLite::SQLite3
        nlohmann_json::nlohmann_json
        spdlog::spdlog
)

# ---------------------------------------------------------
# Windows settings
# ---------------------------------------------------------

if(WIN32)
    target_compile_definitions(MX_Central
        PRIVATE
            NOMINMAX
            WIN32_LEAN_AND_MEAN
    )
endif()

# ---------------------------------------------------------
# Output directories
# ---------------------------------------------------------

set_target_properties(MX_Central PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# ---------------------------------------------------------
# Compiler warnings
# ---------------------------------------------------------

if(MSVC)
    target_compile_options(MX_Central PRIVATE
        /W4
        /permissive-
    )
else()
    target_compile_options(MX_Central PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()