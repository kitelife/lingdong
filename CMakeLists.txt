cmake_minimum_required(VERSION 3.30)
project(lingdong)

set(CMAKE_CXX_STANDARD 17)

include(FetchContent)

FetchContent_Declare(
        yaml-cpp
        GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
        GIT_TAG 0.8.0
)
FetchContent_MakeAvailable(yaml-cpp)

FetchContent_Declare(
        leveldb
        GIT_REPOSITORY https://github.com/google/leveldb.git
        GIT_TAG 1.23
)
FetchContent_MakeAvailable(leveldb)

add_subdirectory(thirdparty/fmt)
add_subdirectory(thirdparty/abseil)
add_subdirectory(thirdparty/inja)

add_executable(lingdong src/main.cpp
        src/md_parser.cpp
        src/md_parser.h
        src/page_render.cpp
        src/page_render.h
        src/publisher.cpp
        src/publisher.h
)

target_link_libraries(lingdong absl::strings)
target_link_libraries(lingdong fmt::fmt)
target_link_libraries(lingdong inja)
target_link_libraries(lingdong yaml-cpp::yaml-cpp)
target_link_libraries(lingdong leveldb)