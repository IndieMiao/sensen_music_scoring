# iOS 接入指南

把 SingScoring SDK 接到你自己的 iOS 应用里。传入一个歌曲 zip 和麦克风 PCM，
返回一个整数分数。

相关参考：
- [ABI.md](ABI.md) —— iOS 绑定封装的底层 C 接口。
- [bindings/ios/README.md](../bindings/ios/README.md) —— 框架本身的说明。
- [demo-ios/](../demo-ios) —— 一个完整使用本 SDK 的 SwiftUI 参考工程。

## 1. 你会拿到什么

- 一个 `SingScoring.xcframework`，内含 iOS 真机（`arm64`）和模拟器
  （`arm64` + `x86_64`）两个切片。
- Obj-C 类 `SSCSession`，在 Swift 里以 `SingScoringSession` 暴露
  （通过 `NS_SWIFT_NAME`）。保留 Obj-C 名是为了避免和 framework 模块
  同名冲突。
- 没有额外运行时依赖。C++ 标准库随框架一起打包。
- 分数区间：`[10, 99]`。及格线 ≥ 60。

## 2. 环境要求

| | |
| --- | --- |
| iOS deployment target | 13.0（框架默认，构建时可调）／demo 要求 15.0 |
| 架构 | 真机 `arm64`；模拟器 `arm64` + `x86_64` |
| Xcode | 15 或更高 |
| Swift | 5.0 或兼容版本 |
| `NSMicrophoneUsageDescription` | 必填（用到麦克风采集时） |

## 3. 获取 xcframework

**从源码构建**（需要 macOS + Xcode + CMake 3.22+）：

```bash
./scripts/build-ios-xcframework.sh
# 产物：build-ios/SingScoring.xcframework
```

可调的环境变量：

- `CONFIG=Debug` —— 默认 `Release`。
- `MIN_IOS=15.0` —— 默认 `13.0`；如果你的 App 最低系统版本更高，
  拉上来能拿到更好的优化。

脚本会分别构建 device 切片和 simulator 切片，再用 `xcodebuild
-create-xcframework` 合成 xcframework。

**从 CI 下载** —— 每次 push 到 `main`，CI 目前只跑 `desktop-tests` 和
`android-build` 两个 job；iOS 产物暂未上 CI。需要跨平台分发时请自建
macOS runner 并把 `build-ios-xcframework.sh` 的产物上传为 artifact。

## 4. 接入 Xcode 工程

### 方案 A：直接拖入 Xcode（最常见）

1. 把 `build-ios/SingScoring.xcframework` 拖到 Xcode 工程 Navigator。
2. 在 target 的 **General → Frameworks, Libraries, and Embedded Content**
   里，把它的 embed 选项设为 **Embed & Sign**。
   > 这是动态框架，必须 embed，否则运行时会报
   > `dyld: Library not loaded`。
3. Swift 代码里 `import SingScoring` 即可使用。

### 方案 B：XcodeGen / project.yml

demo 工程用的就是这套（`demo-ios/project.yml`）：

```yaml
dependencies:
  - framework: ../build-ios/SingScoring.xcframework
    embed: true
    codeSign: true
```

### 方案 C：Swift Package（手工引用本地二进制）

在你的 `Package.swift` 里：

```swift
.binaryTarget(
    name: "SingScoring",
    path: "path/to/SingScoring.xcframework"
)
```

然后在目标的 `dependencies` 里引用 `"SingScoring"`。

## 5. Info.plist 配置

麦克风用途说明是硬性要求，缺了会在首次请求权限时直接 crash：

```xml
<key>NSMicrophoneUsageDescription</key>
<string>用于录制你的演唱，并与参考旋律比对打分。</string>
```

demo 里的措辞参考 `demo-ios/project.yml`。

## 6. API 参考

头文件：`bindings/ios/include/SingScoring/SSCSession.h`。

### One-shot（推荐）

```swift
import SingScoring

let score = SingScoringSession.score(
    zipPath: zipURL.path,        // 本地磁盘绝对路径
    samples: pcm,                // 单声道 float32 PCM，第 0 个样本 = MIDI t=0
    count: pcm.count,
    sampleRate: 44100            // AVAudioEngine 给你的采样率
)
// score ∈ [10, 99]；60 为及格线
```

95% 的场景用这个。把副歌录进一个 `[Float]`，一次调用打完分。

Swift `Array<Float>` 传 C 指针时直接 `samples: pcm` 就行（编译器会把
数组桥接为 `UnsafePointer<Float>`）；或者显式 `pcm.withUnsafeBufferPointer
{ buf in ... buf.baseAddress! ... }`。

### 流式（确实没法先缓存再打分时才用）

```swift
guard let session = SingScoringSession(zipPath: zipURL.path) else {
    throw SomeError.badZip
}

// 在你的 AVAudioEngine tap 回调里：
if let channel = buffer.floatChannelData?[0] {
    session.feedPCM(channel,
                    count: Int(buffer.frameLength),
                    sampleRate: Int(format.sampleRate))
}

// 用户停止演唱时：
let score = session.finalizeScore()
// session 随后不再接受 feedPCM；ARC 回收它即可。
```

`finalizeScore()` 只能调用一次。之后继续 `feedPCM` 会被忽略。

### 工具方法

```swift
SingScoringSession.sdkVersion              // 例如 "0.4.0"
SingScoringSession.melodyEndMs(zipPath: p) // 打分时长（毫秒），失败返回 -1
```

用 `melodyEndMs` 在参考旋律结束时自动停止采集。**不要**使用
`json.duration`（MP3 时长）或 LRC 最后一行时间戳 —— 原因见 CLAUDE.md 的
"Scoring design invariants" 一节。

## 7. 音频格式

以下要求是硬性的：

- **单声道**。立体声请自己先下混再调。
- **32 位浮点 PCM**，取值范围 `[-1.0, 1.0]`。
- **采样率**：传入你实际用的采样率即可。引擎能处理 ~16 kHz 及以上；
  demo 用 `AVAudioEngine.inputNode` 协商出来的值（真机一般 48000，
  模拟器一般 44100）。中途切换采样率会被丢弃。
- **第 0 个样本 = MIDI t=0**。SDK 不会对齐、不会裁掉起始静音、也没有
  offset 参数。你自己保证麦克风采集和歌词滚动同步开始。demo 在开始
  采集前放了一个 3 秒的倒计时画面。

录音器参考实现：`demo-ios/SingScoringDemo/AudioRecorder.swift`
—— `AVAudioSession` 设成 `.record / .measurement` 模式，
`inputNode` 装一个 tap，把 `floatChannelData?[0]` 拼起来。

## 8. 歌曲 zip 结构

SDK 要求 zip 至少包含：

- 一份 MIDI —— 单音轨、单音旋律，音高在 MIDI 48–75（约 130–620 Hz）。
- 一份 `json` 描述文件，引擎会读其中的元数据。

仓库根目录 `SongHighlightSamples/` 下的样例 zip 就是 demo 和单元测试用的
fixture。你传给 SDK 的是 **zip 文件路径**，不是解压后的目录 —— 引擎直接
从 zip 里读。

zip 如果来自网络（`URLSession` 下载），请先落到 `NSTemporaryDirectory()`
或 `FileManager.default.urls(for: .cachesDirectory, ...)` 下，再把绝对
路径传给 SDK。

## 9. 端到端示例

```swift
import AVFoundation
import SingScoring

@MainActor
final class RecordAndScore {
    private let recorder = AudioRecorder()  // 见 demo-ios 的实现

    func start() async -> Bool {
        await recorder.start { msg in print("mic error: \(msg)") }
    }

    func stopAndScore(zipPath: String) async -> Int {
        let capture = recorder.stop()
        // 一次性打分涉及磁盘 IO 和 FFT，挪到后台线程
        return await Task.detached(priority: .userInitiated) {
            SingScoringSession.score(
                zipPath: zipPath,
                samples: capture.pcm,
                count: capture.pcm.count,
                sampleRate: capture.sampleRate
            )
        }.value
    }
}
```

## 10. 分数重映射（UI 层，可选）

SDK 返回的是 `[10, 99]` 的"原始分"，及格线 60。为了在界面上给到更友好的
观感（把及格带压扁、把高分段拉开），demo 在 **展示层** 对原始分再做一次
分段线性重映射 —— **引擎、ABI、测试统统不受影响**，只是把最终渲染的
数字换了个刻度。

Android demo（`MainActivity.remapScore`，Kotlin）和 iOS demo
（`AppState.swift` 中的 `remapScore`，Swift）公式完全一致：

| 原始分 `s`          | 显示分     | 公式                                    |
| ------------------- | ---------- | --------------------------------------- |
| `s < 15`            | `1`        | 下限钳制（SDK 本身下限是 10）           |
| `15 ≤ s ≤ 59`       | `[1, 60]`  | `1 + round((s - 15) * 59 / 44)`         |
| `60 ≤ s ≤ 70`       | `[60, 95]` | `60 + round((s - 60) * 35 / 10)`        |
| `71 ≤ s ≤ 99`       | `[96, 100]`| `min(100, 96 + round((s - 71) * 4 / 29))` |

边界检查：`14 → 1`、`15 → 1`、`59 → 60`、`60 → 60`、`70 → 95`、
`71 → 96`、`99 → 100`。两刻度在 60 分共享及格线。

iOS demo 的实现（`demo-ios/SingScoringDemo/AppState.swift`）：

```swift
func remapScore(_ raw: Int) -> Int {
    if raw < 15 { return 1 }
    if raw <= 59 { return 1 + Int((Double(raw - 15) * 59.0 / 44.0).rounded()) }
    if raw <= 70 { return 60 + Int((Double(raw - 60) * 35.0 / 10.0).rounded()) }
    return min(100, 96 + Int((Double(raw - 71) * 4.0 / 29.0).rounded()))
}
```

结果页（`ResultView`）默认展示重映射后的分数，提供一个 toggle 切回原始分
方便对照 —— 对应 Android demo 结果页上的 "Show raw score / Show new score"
按钮。

**注意事项**：

- 重映射是纯 UI 行为。**不要**把它塞进 SDK 或跨进程传输层 —— `ss_score`
  返回的永远是 `[10, 99]` 的原始分，跨端协议、打点、服务端存储都应该
  以原始分为准，显示层再做转换。
- 及格判断建议仍然以 **原始分 ≥ 60** 为准。重映射后 59→60 会让边界用户
  从"不及格"翻到"及格"；是否跟随显示分切换色彩/文案是产品决定，引擎
  保持中立。
- 公式需要和 Android demo 保持同步。改公式时同时更新 Kotlin 和 Swift
  两份实现，并把边界值过一遍。

## 11. 版本管理

`SingScoringSession.sdkVersion` 返回 SDK 版本号。Obj-C / Swift API 在
补丁版本间稳定；真正的"单一事实来源"是背后的 C ABI —— 参见
[ABI.md](ABI.md)。升级只需把新的 `SingScoring.xcframework` 替换进工程
即可（注意 embed & sign 的配置别丢）。

## 常见坑

- **必须 Embed & Sign**。只 Link 不 Embed 会在启动时 `dyld` 报错。
- **模拟器切片里没有 `armv7`**。只支持 `arm64` 和 `x86_64`。Apple
  Silicon Mac 上的模拟器跑的是 `arm64` 切片；Intel Mac 上的模拟器
  跑的是 `x86_64` 切片。
- **录音时不要放伴奏**。用户如果从扬声器听到参考 MP3，麦克风串音
  会把分数拉高 —— 伴奏的音高本来就和 MIDI 完美吻合。demo 出于这个原因
  没有播放伴奏。
- **`.zip` 必须落在本地磁盘**。引擎用 stdio 打开它，不支持 `file://`
  URL 字符串（要用 `.path`），也不支持从网络流式读取。
- **一个 session 对应一首歌**。不要把 A 歌的采样喂进为 B 歌打开的
  session。用 one-shot 接口就不会踩这个坑。
- **采样率要与实际 PCM 匹配**。`AVAudioEngine` 返回的采样率可能不是
  44100（真机常见 48000）。直接把 `inputNode.outputFormat(forBus: 0)
  .sampleRate` 强转 `Int` 传进去即可，不要硬编码。
- **已知有问题的样例**：`7104926136466570.zip` 声明 74 BPM，实际约
  120 BPM。对它打分结果不可信；遇到请跳过。
