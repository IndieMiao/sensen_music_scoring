# Android 接入指南

把 SingScoring SDK 接到你自己的 Android 应用里。你传入一个歌曲 zip
和麦克风 PCM，它返回一个整数分数。

相关参考：
- [ABI.md](ABI.md) —— Android 绑定封装的底层 C 接口。
- [demo-android/](../demo-android) —— 一个完整使用本 SDK 的参考工程。

## 1. 你会拿到什么

- Kotlin 包名 `com.sensen.singscoring` 下的 `SingScoringSession` 类。
- 每个 ABI 一份 `libsingscoring.so`，已随 AAR 打包。无额外运行时依赖
  —— C++ 标准库静态链接进来（`-DANDROID_STL=c++_static`）。
- 分数区间：`[10, 99]`。及格线 ≥ 60。

## 2. 环境要求

| | |
| --- | --- |
| minSdk | 24 |
| ABI | arm64-v8a、armeabi-v7a、x86_64 |
| Kotlin | 2.0.21 或兼容版本 |
| 编译方的 JDK | 17 |
| `RECORD_AUDIO` 权限 | 需要（麦克风采集时在运行时申请） |

## 3. 获取 AAR

三选一：

**从源码构建**

```bash
export JAVA_HOME=/path/to/jdk-17   # 或者 Android Studio 自带的 JBR
./gradlew :singscoring:assembleRelease
# 产物：bindings/android/build/outputs/aar/singscoring-release.aar
```

**从 CI 下载** —— 每次 push 到 `main` 都会把 AAR 作为 workflow 产物上传。
参见 `.github/workflows/ci.yml` 里的 `android-build` job。

**本地 Maven 发布**（可选）—— 暂未配置。如需要可在
`bindings/android/build.gradle.kts` 里加 `maven-publish` 插件。

## 4. Gradle 配置

把 AAR 放到 `app/libs/`，然后：

```kotlin
// app/build.gradle.kts
android {
    defaultConfig {
        minSdk = 24
        ndk { abiFilters += listOf("arm64-v8a", "armeabi-v7a", "x86_64") }
    }
}

dependencies {
    implementation(files("libs/singscoring-release.aar"))
}
```

后续接入 Maven 后，把 `files(...)` 替换为
`implementation("com.sensen:singscoring:<version>")` 即可。

## 5. 权限

```xml
<!-- AndroidManifest.xml -->
<uses-permission android:name="android.permission.RECORD_AUDIO" />
```

采集前需要在运行时申请 —— 参见 demo 里 `MainActivity.kt` 中
`ActivityResultContracts.RequestPermission` 的用法。

## 6. API 参考

Kotlin 源码：`bindings/android/src/main/kotlin/com/sensen/singscoring/SingScoringSession.kt`。

### One-shot（推荐）

```kotlin
val score: Int = SingScoringSession.score(
    zipPath = "/data/.../song.zip",   // 绝对路径
    samples = floats,                 // 单声道 float32 PCM，第 0 个样本 = MIDI t=0
    sampleRate = 44100,               // AudioRecord 给你的采样率
)
// score ∈ [10, 99]；60 为及格线
```

95% 的场景用这个就够了。把副歌录进一个 `FloatArray`，一次调用打完分。

### 流式（确实没法先缓存再打分时才用）

```kotlin
SingScoringSession.open(zipPath).use { session ->
    while (recording) {
        val n = audioRecord.read(buf, 0, buf.size, AudioRecord.READ_BLOCKING)
        session.feedPcm(buf, sampleRate = 44100, count = n)
    }
    val score = session.finalizeScore()
}
```

`use { }` 会自动调用 `close()` —— 别漏掉，否则会泄漏 native 句柄。

### 工具方法

```kotlin
SingScoringSession.version           // 例如 "0.3.0"
SingScoringSession.melodyEndMs(zip)  // 打分时长（毫秒），失败返回 -1
```

用 `melodyEndMs` 在参考旋律结束时自动停止采集。**不要**使用
`json.duration`（MP3 时长）或 LRC 最后一行时间戳 —— 原因见 CLAUDE.md 的
"Scoring design invariants" 一节。

## 7. 音频格式

以下要求是硬性的：

- **单声道**。立体声请自己先下混再调 `feedPcm` / `score`。
- **32 位浮点 PCM**，取值范围 `[-1.0, 1.0]`。
- **采样率**：传 `AudioRecord` 协商出来的那个即可。引擎能处理 ~16 kHz
  及以上；demo 用的是 44100。
- **第 0 个样本 = MIDI t=0**。SDK 不会对齐、不会裁掉起始静音、也没有
  offset 参数。你自己保证麦克风采集和歌词滚动同步开始。demo 在开始
  采集前放了一个 3 秒的倒计时画面。

录音器参考实现：
`demo-android/src/main/kotlin/com/sensen/singscoring/demo/AudioRecorder.kt`
—— `CHANNEL_IN_MONO` + `ENCODING_PCM_FLOAT`，44100 Hz。

## 8. 歌曲 zip 结构

SDK 要求 zip 至少包含：

- 一份 MIDI —— 单音轨、单音旋律，音高在 MIDI 48–75（约 130–620 Hz）。
- 一份 `json` 描述文件，引擎会读其中的元数据。

仓库根目录 `SongHighlightSamples/` 下的样例 zip 就是 demo 和单元测试用的
fixture。你传给 SDK 的是 **zip 文件路径**，不是解压后的目录 —— 引擎直接
从 zip 里读。

## 9. 端到端示例

```kotlin
class RecordAndScore(private val context: Context) {
    private val pcm = ArrayList<FloatArray>()
    private var recorder: AudioRecorder? = null
    private val sampleRate = 44100

    fun start() {
        recorder = AudioRecorder(sampleRate) { samples, count ->
            pcm.add(samples.copyOf(count))
        }.also { it.start() }
    }

    fun stopAndScore(zipPath: String): Int {
        recorder?.stop(); recorder = null
        val flat = FloatArray(pcm.sumOf { it.size })
        var off = 0
        for (chunk in pcm) { chunk.copyInto(flat, off); off += chunk.size }
        pcm.clear()
        return SingScoringSession.score(zipPath, flat, sampleRate)
    }
}
```

`stopAndScore` 请放到子线程里跑 —— 一次性打分涉及磁盘 IO 和 FFT。

## 10. 版本管理

`SingScoringSession.version` 返回 SDK 版本号。Kotlin API 在补丁版本间稳定；
真正的"单一事实来源"是背后的 C ABI —— 参见 [ABI.md](ABI.md)。升级只需
把新的 AAR 丢进去替换即可。

## 常见坑

- **录音时不要放伴奏**。用户如果从设备扬声器听到参考 MP3，麦克风串音
  会把分数拉高 —— 伴奏的音高本来就和 MIDI 完美吻合。demo 出于这个原因
  没有播放伴奏。
- **`.zip` 必须落在本地磁盘**。引擎用 stdio 打开它，不支持 `content://`
  URI，也不支持从网络流式读取。先下载到 `cacheDir`（参考
  `SongStaging.kt`）。
- **一个 session 对应一首歌**。不要把 A 歌的采样喂进为 B 歌打开的
  session。用 one-shot 接口就不会踩这个坑。
- **已知有问题的样例**：`7104926136466570.zip` 声明 74 BPM，实际约
  120 BPM。对它打分结果不可信；遇到请跳过。
