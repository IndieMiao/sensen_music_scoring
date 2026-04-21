cmake_minimum_required(VERSION 3.22)
project(singscoring_jni LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Pull in the portable core from ../../../../../core
set(SINGSCORING_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(
    ${CMAKE_CURRENT_SOURCE_DIR}/../../../../../core
    ${CMAKE_BINARY_DIR}/singscoring_core
)

add_library(singscoring SHARED jni.cpp)

target_link_libraries(singscoring
    PRIVATE singscoring_core
    PRIVATE log
)

target_compile_options(singscoring PRIVATE -Wall -Wextra)
