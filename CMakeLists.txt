cmake_minimum_required(VERSION 3.26)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(lingdong LANGUAGES CXX)

find_package(gflags REQUIRED)
find_package(fmt REQUIRED)
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
FetchContent_Declare(
        roaring
        GIT_REPOSITORY https://github.com/RoaringBitmap/CRoaring.git
        GIT_TAG v4.3.5
        GIT_SHALLOW TRUE
)
set(ENABLE_ROARING_TESTS OFF CACHE INTERNAL "")
set(ROARING_BUILD_STATIC ON CACHE INTERNAL "")

FetchContent_MakeAvailable(jsoncpp cpr googletest libuv roaring)

FetchContent_GetProperties(roaring)
SET(CPP_ROARING_HEADERS ${roaring_SOURCE_DIR}/cpp/roaring64map.hh  ${roaring_SOURCE_DIR}/cpp/roaring.hh)
file(COPY ${CPP_ROARING_HEADERS} DESTINATION ${roaring_SOURCE_DIR}/include/roaring)

add_subdirectory(third_party/abseil)
# https://github.com/pantor/inja, 另一个选择是 https://github.com/jinja2cpp/Jinja2Cpp
add_subdirectory(third_party/inja)
# https://github.com/ToruNiina/toml11
add_subdirectory(third_party/toml11)
# https://github.com/SRombauts/SQLiteCpp
add_subdirectory(third_party/SQLiteCpp)
# https://github.com/Tessil/robin-map
add_subdirectory(third_party/robin-map)
# https://github.com/xtensor-stack/xsimd
add_subdirectory(third_party/xsimd)

if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(-fdiagnostics-color=always)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_options(-fcolor-diagnostics)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2 -mavx2")
endif()

include(third_party/cpuinfo/cpuinfo.cmake)

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
        roaring
        cpuinfo
        xsimd
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

        src/parser/markdown_test.cpp
        src/parser/markdown.h
        src/parser/markdown.cpp

        src/utils/simd.hpp
        src/utils/simd_test.cpp

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
        #
        tsl::robin_map
        xsimd
        cpuinfo
)
include(GoogleTest)
gtest_discover_tests(test_lingdong)
