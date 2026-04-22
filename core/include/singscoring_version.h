#ifndef SINGSCORING_VERSION_H
#define SINGSCORING_VERSION_H

// Semantic version. Bump these together with a CHANGELOG entry.
// Pre-1.0 the ABI may change between minor versions; from 1.0.0 onward only
// major bumps may break the C ABI declared in singscoring.h.
#define SSC_VERSION_MAJOR 0
#define SSC_VERSION_MINOR 4
#define SSC_VERSION_PATCH 0

#define SSC_STRINGIFY_(x) #x
#define SSC_STRINGIFY(x)  SSC_STRINGIFY_(x)

#define SSC_VERSION_STRING \
    SSC_STRINGIFY(SSC_VERSION_MAJOR) "." \
    SSC_STRINGIFY(SSC_VERSION_MINOR) "." \
    SSC_STRINGIFY(SSC_VERSION_PATCH)

#endif
