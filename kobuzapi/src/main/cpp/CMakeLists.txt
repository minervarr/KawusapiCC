cmake_minimum_required(VERSION 3.22.1)
project(kobuzapi LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Reusable native engine (HTTP, tagging, utils) consumed from the submodule.
set(AE_BUILD_JNI OFF CACHE BOOL "" FORCE)
set(AE_BUILD_ARCHIVE OFF CACHE BOOL "" FORCE)
set(AE_BUILD_NET ON CACHE BOOL "" FORCE)
set(AE_BUILD_TAG ON CACHE BOOL "" FORCE)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../engine/archive_engine/src/main/cpp
    ${CMAKE_CURRENT_BINARY_DIR}/archive_engine
)

find_library(log-lib log)

add_library(
    kobuzapi
    SHARED
    core/errors.cc
    core/json.cc
    core/credentials.cc
    api/requests.cc
    api/service.cc
    metadata/extractor.cc
    metadata/performers.cc
    metadata/embedder.cc
    download/download.cc
    kobuzapi_jni.cpp
)

target_include_directories(kobuzapi PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)

# Release-only optimizations matching the engine: thin LTO, hidden symbols,
# dead-code stripping.
target_compile_options(kobuzapi PRIVATE
    $<$<CONFIG:Release>:-O3 -flto=thin -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -fomit-frame-pointer>
)
target_link_options(kobuzapi PRIVATE
    $<$<CONFIG:Release>:-flto=thin -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,--strip-all>
)

target_link_libraries(kobuzapi
    ae_util
    ae_net
    ae_tag
    ${log-lib}
)
