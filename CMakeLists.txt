cmake_minimum_required(VERSION 3.24)

project(batonics_mbo
    VERSION 0.1.0
    LANGUAGES CXX)

# C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Treat warnings seriously (you can tweak later)
if (MSVC)
    add_compile_options(/W4 /permissive-)
else ()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif ()

include(FetchContent)

# --- Databento C++ library (DBN decoding) ---
# Pin to a specific tag for reproducible builds.
FetchContent_Declare(
    databento
    GIT_REPOSITORY https://github.com/databento/databento-cpp.git
    GIT_TAG HEAD
)

# --- nlohmann::json (header-only JSON lib) ---
# FetchContent_Declare(
#     nlohmann_json
#     GIT_REPOSITORY https://github.com/nlohmann/json.git
#     GIT_TAG v3.11.3  # example stable release
# )

FetchContent_MakeAvailable(databento)

# --- Source files ---
set(MBO_SOURCES
    src/main.cpp
    src/config.cpp
    src/dbn_reader.cpp
    src/order_book.cpp
    src/net.cpp
)

set(MBO_HEADERS
    src/config.hpp
    src/dbn_reader.hpp
    src/order_book.hpp
    src/net.hpp
)

add_executable(mbo_app ${MBO_SOURCES} ${MBO_HEADERS})

target_include_directories(mbo_app
    PRIVATE
        src
)

target_link_libraries(mbo_app
    PRIVATE
        databento::databento
        # nlohmann_json::nlohmann_json
)

add_executable(dbn_reader_test
    src/dbn_reader_test_main.cpp
    src/dbn_reader.cpp
)

target_include_directories(dbn_reader_test PRIVATE src)

target_link_libraries(dbn_reader_test
    PRIVATE
        databento::databento
)

# Optional: separate Release as default if not set
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif ()
