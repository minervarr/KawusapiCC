cmake_minimum_required(VERSION 3.22.1)
project(kobuzapi_core LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

# Reusable native engine (HTTP, tagging, utils) from the archive_engine
# submodule. On desktop, the consumer must already have CURL::libcurl and a
# `tag` (TagLib) target in scope (see archive_engine/core/net and .../tag's
# CMakeLists.txt) — e.g. streamer's root CMakeLists.txt add_subdirectory()s
# third_party/curl and third_party/taglib before add_subdirectory()'ing this
# file. On Android those are cross-compiled automatically.
set(AE_BUILD_ARCHIVE OFF CACHE BOOL "" FORCE)
set(AE_BUILD_NET ON CACHE BOOL "" FORCE)
set(AE_BUILD_TAG ON CACHE BOOL "" FORCE)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../engine/archive_engine/core
    ${CMAKE_CURRENT_BINARY_DIR}/archive_engine
)

# Platform-agnostic API library — no JNI here (see platform/android/).
add_library(kobuzapi_core STATIC
    core/errors.cc
    core/json.cc
    core/credentials.cc
    api/requests.cc
    api/service.cc
    metadata/extractor.cc
    metadata/performers.cc
    metadata/embedder.cc
    download/download.cc
)

target_include_directories(kobuzapi_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
)
target_compile_features(kobuzapi_core PUBLIC cxx_std_17)

target_link_libraries(kobuzapi_core PUBLIC ae_util ae_net ae_tag)
