cmake_minimum_required(VERSION 3.28)

project(lingdong LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

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
find_package(roaring REQUIRED)
find_package(mimalloc REQUIRED)
find_package(maxminddb REQUIRED)
find_package(croncpp REQUIRED)
find_package(cryptopp REQUIRED)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(CMAKE_C_COMPILER "/usr/bin/clang")
    set(CMAKE_CXX_COMPILER "/usr/bin/clang++")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -msse4.2 -mavx2 -mfma")
endif()

add_compile_options(-O0 -g)
if("${CMAKE_CXX_COMPILER_ID}" STREQUAL "GNU")
    add_compile_options(-fdiagnostics-color=always)
elseif("${CMAKE_CXX_COMPILER_ID}" STREQUAL "Clang")
    add_compile_options(-fcolor-diagnostics -ftime-trace)
    set(CMAKE_C_FLAGS "${CMAKE_CXX_FLAGS} -ftime-trace")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -ftime-trace")
endif()

# https://github.com/pantor/inja, 另一个选择是 https://github.com/jinja2cpp/Jinja2Cpp
# conan 上的版本太老了
add_subdirectory(third_party/inja)

# https://github.com/yanyiwu/cppjieba
add_subdirectory(third_party/cppjieba)
# https://cmake.org/cmake/help/latest/command/include_directories.html#command:include_directories
include_directories(src
        third_party/cppjieba/include
        third_party/cppjieba/deps/limonp/include
)

# https://github.com/nmslib/hnswlib
add_subdirectory(third_party/hnswlib)
include_directories(third_party/hnswlib)

add_executable(lingdong src/main.cpp
        src/maker.hpp
        src/web.hpp
        src/config.hpp
        src/context.hpp
        src/parser/markdown.cpp
        src/parser/markdown.h

        src/service/protocol.h
        src/service/server.hpp

        src/service/http/base_app.hpp
        src/service/http/handler.hpp
        src/service/http/protocol.hpp
        src/service/http/router.hpp

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
        src/plugin/bemathjax.hpp
        src/plugin/plugins.hpp

        src/utils/taoli.hpp
        src/utils/strings.hpp
        src/utils/guard.hpp
        src/utils/rate_limit.hpp
        src/utils/blocking_queue.hpp
        src/utils/executor.hpp
        src/utils/tokenizer.hpp
        src/utils/ollama.hpp
        src/utils/task_scheduler.hpp
)

if (DEFINED ENV{ENABLE_HN_SEARCH})
    target_compile_definitions(lingdong PRIVATE ENABLE_HN_SEARCH)
endif()

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
        roaring::roaring
        xsimd
        tinyxml2::tinyxml2
        fmt::fmt
        ZLIB::ZLIB
        cppjieba
        mimalloc-static
        croncpp::croncpp
)

add_executable(hacker_news src/task/hacker_news.cpp
        src/utils/executor.hpp
        src/utils/blocking_queue.hpp
        src/utils/perf.hpp
        src/utils/simd.hpp
)
target_link_libraries(hacker_news
        cpr::cpr
        inja
        spdlog::spdlog
        gflags::gflags
        hnswlib
        abseil::abseil
        xsimd
        tsl::robin_map
)

add_executable(image2base64 src/task/image2base64.cpp
        src/utils/image.hpp
)
target_link_libraries(image2base64
        gflags::gflags
        abseil::abseil
        spdlog::spdlog
)

add_executable(viewer_stat src/task/viewer_stat.cpp
        src/utils/guard.hpp
        src/utils/mmdb.hpp
        src/storage/local_sqlite.h
        src/storage/local_sqlite.cpp
)
target_link_libraries(viewer_stat
        spdlog::spdlog
        maxminddb::maxminddb
        gflags::gflags
        fmt::fmt
        SQLiteCpp
        sqlite3
        abseil::abseil
        tsl::robin_map
        inja
        cpr::cpr
)
add_executable(nebula_get src/task/nebula_get.cpp
    src/utils/helper.hpp
)
target_link_libraries(nebula_get
        gflags::gflags
        cpr::cpr
        cryptopp::cryptopp
        nlohmann_json
        spdlog::spdlog
)

# for testing
enable_testing()
add_executable(test_lingdong
        src/config.hpp
        src/context.hpp
        src/plugin/plantuml.hpp
        src/plugin/mermaid.hpp
        src/plugin/smms.hpp
        src/parser/markdown.h
        src/parser/markdown.cpp
        src/utils/simd.hpp
        src/utils/strings.hpp
        src/utils/time.hpp
        src/utils/tokenizer.hpp
        src/utils/ollama.hpp
        src/utils/image.hpp
        src/utils/task_scheduler.hpp

        tests/plantuml_test.cpp
        tests/smms_test.cpp
        tests/strings_test.cpp
        tests/time_test.cpp
        tests/markdown_test.cpp
        tests/simd_test.cpp
        tests/tokenizer_test.cpp
        tests/ollama_test.cpp
        tests/image_test.cpp
        tests/format_test.cpp
        tests/task_scheduler_test.cpp
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
        cppjieba
        croncpp::croncpp
)
include(GoogleTest)
gtest_discover_tests(test_lingdong)
