cmake_minimum_required(VERSION 3.22.1)
project(kobuzapi_android LANGUAGES C CXX)

set(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

add_subdirectory(${REPO_ROOT}/core ${CMAKE_CURRENT_BINARY_DIR}/core)

find_library(log-lib log)

add_library(kobuzapi SHARED kobuzapi_jni.cpp)

# Release-only optimizations matching the engine: thin LTO, hidden symbols,
# dead-code stripping.
target_compile_options(kobuzapi PRIVATE
    $<$<CONFIG:Release>:-O3 -flto=thin -fvisibility=hidden -fvisibility-inlines-hidden -ffunction-sections -fdata-sections -fomit-frame-pointer>
)
target_link_options(kobuzapi PRIVATE
    $<$<CONFIG:Release>:-flto=thin -Wl,--gc-sections -Wl,--exclude-libs,ALL -Wl,--strip-all>
)

target_link_libraries(kobuzapi kobuzapi_core ${log-lib})
