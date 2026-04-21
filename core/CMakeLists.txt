# Portable C++ scoring engine. No platform deps.
# Produces a static library plus the public C ABI header.

add_library(singscoring_core STATIC
    src/session.cpp
    src/version.cpp
)

target_include_directories(singscoring_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_options(singscoring_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

set_target_properties(singscoring_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
