cmake_minimum_required(VERSION 3.30)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(lingdong LANGUAGES CXX)

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
FetchContent_Declare(
        libuv
        GIT_REPOSITORY https://github.com/libuv/libuv.git
        GIT_TAG v1.50.0
)

FetchContent_MakeAvailable(jsoncpp cpr googletest libuv)

add_subdirectory(third_party/abseil)
# https://github.com/pantor/inja, 另一个选择是 https://github.com/jinja2cpp/Jinja2Cpp
add_subdirectory(third_party/inja)
# https://github.com/ToruNiina/toml11
add_subdirectory(third_party/toml11)
# https://github.com/SRombauts/SQLiteCpp
add_subdirectory(third_party/SQLiteCpp)
# https://github.com/Tessil/robin-map
add_subdirectory(third_party/robin-map)

add_executable(lingdong src/main.cpp
        src/maker.hpp
        src/server.hpp
        src/config.hpp
        src/parser/markdown.cpp
        src/parser/markdown.h

        src/plugin/plugin.h
        src/plugin/plantuml.hpp
        src/plugin/mermaid.hpp
        src/plugin/smms.hpp
        src/plugin/typst_cmarker_pdf.hpp
        src/plugin/plugins.hpp

        src/utils/taoli.hpp
        src/utils/strings.hpp
)
target_compile_options(lingdong PRIVATE -O3 -g)

target_link_libraries(
        lingdong
        #
        absl::strings
        absl::hash
        absl::time
        #
        inja
        jsoncpp_static
        toml11::toml11
        cpr::cpr
        zlib
        gflags
        spdlog::spdlog_header_only
        libuv::libuv
        #
        SQLiteCpp
        sqlite3
        pthread
        dl
        #
        tsl::robin_map
)

add_executable(tester src/tester.cpp)
target_link_libraries(
        tester
        #
        SQLiteCpp
        sqlite3
        pthread
        dl
)

# for testing
enable_testing()
add_executable(
        test_lingdong
        src/plugin/plantuml_test.cpp
        src/plugin/smms_test.cpp
        src/utils/strings_test.cpp
        src/utils/time_test.cpp
        src/config.hpp
        src/plugin/plantuml.hpp
        src/plugin/mermaid.hpp
        src/plugin/smms.hpp

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
        inja
        #
        SQLiteCpp
        sqlite3
        pthread
        dl
)
include(GoogleTest)
gtest_discover_tests(test_lingdong)
