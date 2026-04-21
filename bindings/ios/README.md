# iOS binding

Placeholder. Full Xcode scaffolding lands in **Phase 3** once the C++ core is validated on desktop (Phase 1).

## Planned layout

```
bindings/ios/
  SingScoring.xcodeproj/          # Xcode project, builds .xcframework
  SingScoring/                    # Obj-C++ shim + Swift public API
    SingScoring.h                 # umbrella header
    SingScoringSession.mm         # Obj-C++ wrapper around singscoring.h
    SingScoringSession.swift      # Swift-friendly facade
  Package.swift                   # SwiftPM binary target pointer
  scripts/build-xcframework.sh    # invoked by CI
```

## Build command (eventual)

```bash
./scripts/build-xcframework.sh
# outputs: build/SingScoring.xcframework
```

Cannot be built on Windows. This module activates on macOS CI.
