# Portable C++ scoring engine. No platform deps.
# Produces a static library plus the public C ABI header.

set(MINIZ_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../third_party/miniz)

add_library(singscoring_core STATIC
    src/session.cpp
    src/version.cpp
    src/song.cpp
    src/zip_loader.cpp
    src/midi_parser.cpp
    src/lrc_parser.cpp
    src/json_parser.cpp
    ${MINIZ_DIR}/miniz.c
)

target_include_directories(singscoring_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
    PRIVATE ${MINIZ_DIR}
)

target_compile_options(singscoring_core PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-Wall -Wextra -Wpedantic>
    $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

# miniz ships one source file; silence its warnings, don't force them on our code.
set_source_files_properties(${MINIZ_DIR}/miniz.c PROPERTIES
    COMPILE_FLAGS "-w"
)

set_target_properties(singscoring_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
)
