cmake_minimum_required(VERSION 3.22)
project(singscoring LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(SINGSCORING_BUILD_TESTS "Build unit tests" ON)

add_subdirectory(core)

if(SINGSCORING_BUILD_TESTS AND NOT ANDROID AND NOT IOS)
    enable_testing()
    add_subdirectory(tests)
endif()
