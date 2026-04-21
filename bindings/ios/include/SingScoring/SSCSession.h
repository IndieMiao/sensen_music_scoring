#ifndef SINGSCORING_SSC_SESSION_H
#define SINGSCORING_SSC_SESSION_H

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Obj-C / Swift facade over the portable C scoring engine.
///
/// Lifecycle mirrors the other bindings: open with a song zip path, feed mic
/// PCM as it arrives, finalize once to get a score in [10, 99] (pass ≥ 60).
NS_SWIFT_NAME(SingScoringSession)
@interface SSCSession : NSObject

/// SDK version string, e.g. "0.1.0".
@property (class, nonatomic, readonly) NSString *sdkVersion;

/// Open a session from a .zip on disk. Returns nil if the file can't be
/// parsed as a valid song bundle.
- (nullable instancetype)initWithZipPath:(NSString *)zipPath NS_DESIGNATED_INITIALIZER;

- (instancetype)init NS_UNAVAILABLE;
+ (instancetype)new NS_UNAVAILABLE;

/// Feed mono float32 PCM samples. `count` must be ≤ the buffer's length in floats.
/// Safe to call repeatedly from an audio callback thread; internally the samples
/// are copied into the session buffer.
- (void)feedPCM:(const float *)samples
          count:(NSInteger)count
     sampleRate:(NSInteger)sampleRate NS_SWIFT_NAME(feedPCM(_:count:sampleRate:));

/// Finalize and return the score. Subsequent `feedPCM:` calls are ignored.
- (NSInteger)finalizeScore;

@end

NS_ASSUME_NONNULL_END

#endif
