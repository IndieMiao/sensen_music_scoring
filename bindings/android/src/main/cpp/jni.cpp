// JNI glue. Marshals Kotlin calls into the C ABI declared in singscoring.h.

#include <jni.h>
#include <android/log.h>

#include "singscoring.h"

#define LOG_TAG "singscoring"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOG_TAG, __VA_ARGS__)

extern "C" {

JNIEXPORT jlong JNICALL
Java_com_sensen_singscoring_SingScoringSession_nativeOpen(JNIEnv* env, jclass, jstring zipPath) {
    if (!zipPath) return 0;
    const char* path = env->GetStringUTFChars(zipPath, nullptr);
    ss_session* s = ss_open(path);
    env->ReleaseStringUTFChars(zipPath, path);
    return reinterpret_cast<jlong>(s);
}

JNIEXPORT void JNICALL
Java_com_sensen_singscoring_SingScoringSession_nativeFeedPcm(
        JNIEnv* env, jclass, jlong handle, jfloatArray samples, jint count, jint sampleRate) {
    auto* s = reinterpret_cast<ss_session*>(handle);
    if (!s || !samples || count <= 0) return;
    jsize len = env->GetArrayLength(samples);
    if (count > len) count = len;
    jfloat* data = env->GetFloatArrayElements(samples, nullptr);
    ss_feed_pcm(s, data, static_cast<int>(count), static_cast<int>(sampleRate));
    env->ReleaseFloatArrayElements(samples, data, JNI_ABORT);
}

JNIEXPORT jint JNICALL
Java_com_sensen_singscoring_SingScoringSession_nativeFinalize(JNIEnv*, jclass, jlong handle) {
    auto* s = reinterpret_cast<ss_session*>(handle);
    if (!s) return 10;
    return ss_finalize_score(s);
}

JNIEXPORT void JNICALL
Java_com_sensen_singscoring_SingScoringSession_nativeClose(JNIEnv*, jclass, jlong handle) {
    auto* s = reinterpret_cast<ss_session*>(handle);
    ss_close(s);
}

JNIEXPORT jstring JNICALL
Java_com_sensen_singscoring_SingScoringSession_nativeVersion(JNIEnv* env, jclass) {
    return env->NewStringUTF(ss_version());
}

JNIEXPORT jlong JNICALL
Java_com_sensen_singscoring_SingScoringSession_nativeMelodyEndMs(
        JNIEnv* env, jclass, jstring zipPath) {
    if (!zipPath) return -1;
    const char* path = env->GetStringUTFChars(zipPath, nullptr);
    long long ms = ss_melody_end_ms(path);
    env->ReleaseStringUTFChars(zipPath, path);
    return static_cast<jlong>(ms);
}

JNIEXPORT jint JNICALL
Java_com_sensen_singscoring_SingScoringSession_nativeScore(
        JNIEnv* env, jclass, jstring zipPath,
        jfloatArray samples, jint count, jint sampleRate) {
    if (!zipPath) return 10;
    const char* path = env->GetStringUTFChars(zipPath, nullptr);
    int score = 10;
    if (samples && count > 0) {
        jsize len = env->GetArrayLength(samples);
        if (count > len) count = len;
        jfloat* data = env->GetFloatArrayElements(samples, nullptr);
        float first = count > 0 ? data[0] : 0.0f;
        float last  = count > 0 ? data[count - 1] : 0.0f;
        LOGI("nativeScore: count=%d rate=%d first=%.4f last=%.4f zip=%s",
             (int)count, (int)sampleRate, first, last, path);
        score = ss_score(path, data, static_cast<int>(count), static_cast<int>(sampleRate));
        env->ReleaseFloatArrayElements(samples, data, JNI_ABORT);
    } else {
        // Empty PCM still goes through ss_score so error semantics stay identical
        // (open succeeds → feed no-ops → finalize returns 10).
        LOGI("nativeScore: count=0 rate=%d (no samples)", (int)sampleRate);
        score = ss_score(path, nullptr, 0, static_cast<int>(sampleRate));
    }
    LOGI("nativeScore: returning %d", score);
    env->ReleaseStringUTFChars(zipPath, path);
    return score;
}

} // extern "C"
