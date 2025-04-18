cmake_minimum_required(VERSION 3.30)
project(lingdong)

set(CMAKE_CXX_STANDARD 17)

set(ENV{HTTP_PROXY}  "127.0.0.1:7890")
set(ENV{HTTPS_PROXY}  "127.0.0.1:7890")

find_package(gflags REQUIRED)
if(NOT TARGET spdlog)
    # Stand-alone build
    find_package(spdlog REQUIRED)
endif()

include(FetchContent)

FetchContent_Declare(
        jsoncpp
        GIT_REPOSITORY https://github.com/open-source-parsers/jsoncpp.git
        GIT_TAG 1.9.6
)

FetchContent_Declare(
        cpr
        GIT_REPOSITORY https://github.com/libcpr/cpr.git
        GIT_TAG 1.11.2
)

# https://google.github.io/googletest/quickstart-cmake.html
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.16.0
)

FetchContent_MakeAvailable(jsoncpp cpr googletest)

add_subdirectory(third_party/abseil)
add_subdirectory(third_party/inja)
add_subdirectory(third_party/toml11)

add_executable(lingdong src/main.cpp
        src/maker.hpp
        src/server.hpp
        src/config.hpp
        src/parser/parser.h
        src/parser/markdown.cpp
        src/parser/markdown.h

        src/plugin/plugin.h
        src/plugin/plantuml.hpp
        src/plugin/mermaid.hpp

        src/utils/taoli.hpp
        src/utils/strings.hpp
)

target_link_libraries(
        lingdong
        absl::strings
        absl::hash
        absl::time
        inja
        jsoncpp_static
        toml11::toml11
        cpr::cpr
        zlib
        gflags
        spdlog::spdlog_header_only
)

# for testing
enable_testing()
add_executable(
        test_lingdong
        src/plugin/plantuml_test.cpp
        src/utils/strings_test.cpp
        src/utils/time_test.cpp
        src/config.hpp
        src/plugin/plantuml.hpp
        src/plugin/mermaid.hpp

        src/parser/parser.h
        src/parser/markdown.h
        src/parser/markdown.cpp

        src/utils/strings.hpp
        src/utils/time.hpp
)
target_link_libraries(
        test_lingdong
        GTest::gtest_main
        cpr::cpr
        fmt::fmt
        absl::strings
        absl::hash
        absl::time
        zlib
        toml11::toml11
)
include(GoogleTest)
gtest_discover_tests(test_lingdong)