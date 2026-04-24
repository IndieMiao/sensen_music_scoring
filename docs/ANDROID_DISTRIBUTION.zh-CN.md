# Android 绑定交付方式

把本仓库的 Android 绑定（AAR）交付给其它项目使用的三种方式，由简到繁。

相关参考：
- [ANDROID_INTEGRATION.zh-CN.md](ANDROID_INTEGRATION.zh-CN.md) —— 使用方接入 SDK 的指南。
- [ABI.md](ABI.md) —— 稳定性契约。

## 1. 直接分发 AAR 文件（最快）

先构建 **release** 版 AAR（debug 版带 `android:debuggable=true`，不能用于他人 app 的发布构建）：

```bash
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"
./gradlew :singscoring:assembleRelease
# → bindings/android/build/outputs/aar/singscoring-release.aar
```

把下面这些交给对方项目：

- `singscoring-release.aar`
- 简要的 API 说明（Kotlin 入口：`com.sensen.singscoring.SingScoringSession`；C ABI：`core/include/singscoring.h`）

对方在 `app/build.gradle.kts` 里：

```kotlin
dependencies {
    implementation(files("libs/singscoring-release.aar"))
}
```

把 AAR 放到 `app/libs/` 即可。AAR 已经打包好 `arm64-v8a`、`armeabi-v7a`、`x86_64` 三种架构的 `.so` 以及 Kotlin 类，无需额外依赖。

需要告诉使用方的几点：

- `minSdk 24`、`compileSdk 34`
- 需要 Kotlin stdlib（现代 Android 项目默认都有）
- SDK **没有**声明 `RECORD_AUDIO` 权限，由宿主 app 自行声明

## 2. 发布到 Maven 仓库（多项目复用推荐）

当前模块还没配置 `maven-publish`。在 `bindings/android/build.gradle.kts` 里加：

```kotlin
plugins {
    alias(libs.plugins.android.library)
    alias(libs.plugins.kotlin.android)
    `maven-publish`
}

android {
    publishing { singleVariant("release") { withSourcesJar() } }
}

afterEvaluate {
    publishing {
        publications {
            create<MavenPublication>("release") {
                from(components["release"])
                groupId = "com.sensen"
                artifactId = "singscoring"
                version = "0.3.0"  // 与 singscoring_version.h 保持一致
            }
        }
        repositories {
            // 方案 A：Maven Local（本机跨项目使用）
            mavenLocal()
            // 方案 B：私有 Maven（Nexus/Artifactory/GitHub Packages）——需配置凭据
        }
    }
}
```

然后：

```bash
./gradlew :singscoring:publishToMavenLocal
```

使用方：

```kotlin
repositories { mavenLocal(); google(); mavenCentral() }
dependencies { implementation("com.sensen:singscoring:0.3.0") }
```

如果要发布到 **GitHub Packages** 或 **JitPack**，publication 写法一样，只需修改 `repositories { ... }` 里的仓库地址和凭据。

## 3. 作为 Gradle 模块引入（耦合度高的仓库适用）

把 `bindings/android/` 和 `core/` 拷贝进对方仓库（或作为 git submodule），在对方 `settings.gradle.kts` 复用本仓库 `settings.gradle.kts` 的两行：

```kotlin
include(":singscoring")
project(":singscoring").projectDir = file("path/to/bindings/android")
```

然后 `implementation(project(":singscoring"))`。此时对方要自己构建 native 代码——需要本地安装 NDK 27.3 和 CMake 3.31。适合 monorepo，对外部用户来说太重。

## 选型建议

| 场景 | 推荐方式 |
| --- | --- |
| 一次性临时交付，给单个团队 | 方案 1（直接发 AAR） |
| 多个项目复用、需要版本管理 | 方案 2（Maven 发布） |
| 内部 monorepo，需要一起改 native 代码 | 方案 3（Gradle 模块） |
