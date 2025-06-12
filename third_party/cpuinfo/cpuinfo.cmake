Include(FetchContent)
FetchContent_Declare(
        cpuinfo
        GIT_REPOSITORY https://github.com/pytorch/cpuinfo.git
        GIT_TAG d7427551d6531037da216d20cd36feb19ed4905f
)

set(CPUINFO_BUILD_TOOLS OFF CACHE BOOL "Disable some option in the library" FORCE)
set(CPUINFO_BUILD_UNIT_TESTS OFF CACHE BOOL "Disable some option in the library" FORCE)
set(CPUINFO_BUILD_MOCK_TESTS OFF CACHE BOOL "Disable some option in the library" FORCE)
set(CPUINFO_BUILD_BENCHMARKS OFF CACHE BOOL "Disable some option in the library" FORCE)
set(CPUINFO_BUILD_PKG_CONFIG OFF CACHE BOOL "Disable some option in the library" FORCE)

# exclude cpuinfo in vsag installation
FetchContent_GetProperties(cpuinfo)
if(NOT cpuinfo_POPULATED)
    FetchContent_Populate(cpuinfo)
    add_subdirectory(${cpuinfo_SOURCE_DIR} ${cpuinfo_BINARY_DIR} EXCLUDE_FROM_ALL)
endif()

include_directories (${cpuinfo_SOURCE_DIR}/include)

install (
        TARGETS cpuinfo
        ARCHIVE DESTINATION lib
)
