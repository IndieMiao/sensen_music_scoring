# iOS framework target. Only configures when building for an Apple platform via
# an iOS toolchain — otherwise returns early so desktop/Android builds ignore it.

if(NOT CMAKE_SYSTEM_NAME STREQUAL "iOS")
    return()
endif()

set(SINGSCORING_PUBLIC_HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SingScoring/SingScoring.h
    ${CMAKE_CURRENT_SOURCE_DIR}/include/SingScoring/SSCSession.h
)

add_library(SingScoring SHARED
    SSCSession.mm
    ${SINGSCORING_PUBLIC_HEADERS}
)

set_source_files_properties(${SINGSCORING_PUBLIC_HEADERS} PROPERTIES
    MACOSX_PACKAGE_LOCATION Headers
)

target_include_directories(SingScoring
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(SingScoring PRIVATE singscoring_core)

# Build as a versioned framework bundle. The `-create-xcframework` step in the
# driver script consumes these .framework outputs directly.
set_target_properties(SingScoring PROPERTIES
    FRAMEWORK TRUE
    FRAMEWORK_VERSION A
    MACOSX_FRAMEWORK_IDENTIFIER     com.sensen.singscoring
    MACOSX_FRAMEWORK_BUNDLE_VERSION 1
    MACOSX_FRAMEWORK_SHORT_VERSION_STRING 0.4.0
    MACOSX_FRAMEWORK_INFO_PLIST     ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    PUBLIC_HEADER
        "${CMAKE_CURRENT_SOURCE_DIR}/include/SingScoring/SingScoring.h;${CMAKE_CURRENT_SOURCE_DIR}/include/SingScoring/SSCSession.h"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER com.sensen.singscoring
    XCODE_ATTRIBUTE_DEFINES_MODULE YES
    XCODE_ATTRIBUTE_CLANG_ENABLE_MODULES YES
    XCODE_ATTRIBUTE_ENABLE_BITCODE NO
    XCODE_ATTRIBUTE_SKIP_INSTALL NO
    XCODE_ATTRIBUTE_BUILD_LIBRARY_FOR_DISTRIBUTION YES
)
