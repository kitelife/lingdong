cmake_minimum_required(VERSION 3.23)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project(lingdong LANGUAGES CXX)

find_package(gflags REQUIRED)
find_package(fmt REQUIRED)
find_package(spdlog REQUIRED)
find_package(toml11 REQUIRED)
find_package(absl REQUIRED)
find_package(SQLiteCpp REQUIRED)
find_package(tsl-robin-map REQUIRED)
find_package(tinyxml2 REQUIRED)
find_package(xsimd REQUIRED)
find_package(jsoncpp REQUIRED)
find_package(cpr REQUIRED)
find_package(libuv REQUIRED)
find_package(ZLIB REQUIRED)
find_package(GTest REQUIRED)

include(FetchContent)

FetchContent_Declare(
        roaring
        GIT_REPOSITORY https://github.com/RoaringBitmap/CRoaring.git
        GIT_TAG v4.3.5
        GIT_SHALLOW TRUE
)
set(ENABLE_ROARING_TESTS OFF CACHE INTERNAL "")
set(ROARING_BUILD_STATIC ON CACHE INTERNAL "")

FetchContent_MakeAvailable(roaring)

FetchContent_GetProperties(roaring)
SET(CPP_ROARING_HEADERS ${roaring_SOURCE_DIR}/cpp/roaring64map.hh  ${roaring_SOURCE_DIR}/cpp/roaring.hh)
file(COPY ${CPP_ROARING_HEADERS} DESTINATION ${roaring_SOURCE_DIR}/include/roaring)


if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(-fdiagnostics-color=always)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_options(-fcolor-diagnostics)
endif()

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2 -mavx2")
endif()

# https://github.com/pantor/inja, 另一个选择是 https://github.com/jinja2cpp/Jinja2Cpp
# conan 上的版本太老了
add_subdirectory(third_party/inja)

# https://cmake.org/cmake/help/latest/command/include_directories.html#command:include_directories
include_directories(src)

add_executable(lingdong src/main.cpp
        src/maker.hpp
        src/server.hpp
        src/config.hpp
        src/context.hpp
        src/parser/markdown.cpp
        src/parser/markdown.h
        src/service/protocol.h
        src/service/http/protocol.hpp
        src/service/http/router.hpp
        src/service/http/handler.hpp

        src/storage/local_sqlite.h
        src/storage/local_sqlite.cpp

        src/plugin/plugin.h
        src/plugin/plantuml.hpp
        src/plugin/mermaid.hpp
        src/plugin/smms.hpp
        src/plugin/typst_cmarker_pdf.hpp
        src/plugin/giscus.hpp
        src/plugin/highlight.hpp
        src/plugin/mathjax.hpp
        src/plugin/fekatex.hpp
        src/plugin/gtalk.hpp
        src/plugin/plugins.hpp

        src/utils/taoli.hpp
        src/utils/strings.hpp
        src/utils/guard.hpp
        src/utils/rate_limit.hpp
        src/utils/blocking_queue.hpp
        src/utils/executor.hpp
)
target_compile_options(lingdong PRIVATE -O0 -g)

target_link_libraries(
        lingdong
        #
        abseil::abseil
        #
        inja
        JsonCpp::JsonCpp
        toml11::toml11
        cpr::cpr
        gflags::gflags
        spdlog::spdlog
        uv
        #
        SQLiteCpp
        sqlite3
        pthread
        dl
        #
        tsl::robin_map
        roaring
        xsimd
        tinyxml2::tinyxml2
        fmt::fmt
        ZLIB::ZLIB
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
        src/context.hpp
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
        gtest::gtest
        cpr::cpr
        fmt::fmt
        ZLIB::ZLIB
        abseil::abseil
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
        spdlog::spdlog
)
include(GoogleTest)
gtest_discover_tests(test_lingdong)
