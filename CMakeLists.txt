cmake_minimum_required(VERSION 3.30)
project(lingdong)

set(CMAKE_CXX_STANDARD 17)

#set(ENV{HTTP_PROXY}  "127.0.0.1:7890")
#set(ENV{HTTPS_PROXY}  "127.0.0.1:7890")

include(FetchContent)

# https://www.ruanyifeng.com/blog/2016/07/yaml.html
FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
)

FetchContent_Declare(
        leveldb
        GIT_REPOSITORY https://github.com/google/leveldb.git
        GIT_TAG 1.23
)


FetchContent_Declare(
        gflags
        GIT_REPOSITORY https://github.com/gflags/gflags.git
        GIT_TAG v2.2.2
)

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

FetchContent_MakeAvailable(yaml-cpp leveldb gflags jsoncpp cpr)

add_subdirectory(thirdparty/fmt)
add_subdirectory(thirdparty/abseil)
add_subdirectory(thirdparty/inja)
add_subdirectory(thirdparty/toml11)

add_executable(lingdong src/main.cpp
        src/parser/parser.h
        src/parser/markdown.cpp
        src/parser/markdown.h

        src/plugin/plugin.h
        src/plugin/plantuml.cpp
        src/plugin/plantuml.h

        src/utils/taoli.hpp
        src/utils/strings.hpp
)

target_link_libraries(lingdong absl::strings)
target_link_libraries(lingdong fmt::fmt)
target_link_libraries(lingdong inja)
target_link_libraries(lingdong yaml-cpp::yaml-cpp)
target_link_libraries(lingdong leveldb)
target_link_libraries(lingdong gflags)
target_link_libraries(lingdong jsoncpp_static)
target_link_libraries(lingdong toml11::toml11)
target_link_libraries(lingdong cpr::cpr)