# SingScoring — iOS binding

An Obj-C++ shim around the portable C scoring engine, packaged as an
`xcframework` so the same artifact drops into both device and simulator targets.

## Build (macOS only)

```bash
./scripts/build-ios-xcframework.sh
```

Output: `build-ios/SingScoring.xcframework`. Drag that into your Xcode project
(or reference it from a Swift Package).

Environment knobs:

- `CONFIG=Debug` — default is `Release`
- `MIN_IOS=15.0` — default is `13.0`

## Usage

Swift (with `import SingScoring`):

```swift
guard let session = SingScoringSession(zipPath: zipURL.path) else {
    fatalError("failed to open song zip")
}

// From your AVAudioEngine / AudioUnit callback:
samples.withUnsafeBufferPointer { buf in
    session.feedPCM(buf.baseAddress!, count: buf.count, sampleRate: 44100)
}

let score = session.finalizeScore()  // 10...99, pass ≥ 60
```

The class is published to Swift as `SingScoringSession` (see `NS_SWIFT_NAME`
on `SSCSession`); the Obj-C symbol stays `SSCSession` to avoid collision with
the framework module name.

## Microphone capture

The framework is audio-agnostic — it takes a `const float *` and a count.
On iOS you'd typically feed it from `AVAudioEngine.inputNode` tap's
`AVAudioPCMBuffer.floatChannelData`. Sample rate must match what you pass to
`feedPCM:count:sampleRate:`; mid-session rate changes are dropped.
