include(FetchContent)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(singscoring_tests
    test_version.cpp
    test_session.cpp
    test_midi_parser.cpp
    test_lrc_parser.cpp
    test_json_parser.cpp
    test_zip_loader.cpp
    test_song_integration.cpp
)

# Tests poke at internal types (Note, LrcLine, etc). Give them access to core/src/.
target_include_directories(singscoring_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/core/src
)

target_compile_definitions(singscoring_tests PRIVATE
    SSC_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/SongHighlightSamples"
)

target_link_libraries(singscoring_tests PRIVATE
    singscoring_core
    GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(singscoring_tests)
