#import "SSCSession.h"

#include "singscoring.h"

@implementation SSCSession {
    ss_session *_session;
    BOOL        _finalized;
}

+ (NSString *)sdkVersion {
    return [NSString stringWithUTF8String:ss_version()];
}

- (nullable instancetype)initWithZipPath:(NSString *)zipPath {
    self = [super init];
    if (!self) return nil;
    _session = ss_open(zipPath.UTF8String);
    if (!_session) return nil;
    return self;
}

- (void)dealloc {
    if (_session) ss_close(_session);
}

- (void)feedPCM:(const float *)samples
          count:(NSInteger)count
     sampleRate:(NSInteger)sampleRate
{
    if (!_session || _finalized || !samples || count <= 0) return;
    ss_feed_pcm(_session, samples, (int)count, (int)sampleRate);
}

- (NSInteger)finalizeScore {
    if (!_session) return 10;
    _finalized = YES;
    return ss_finalize_score(_session);
}

@end
