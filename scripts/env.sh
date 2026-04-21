#!/usr/bin/env bash
# Source this file (`. scripts/env.sh`) to put the project toolchain on PATH
# for the current shell session. Does not modify system env vars.

export ANDROID_HOME="/c/Users/shjzhang/AppData/Local/Android/Sdk"
export ANDROID_SDK_ROOT="$ANDROID_HOME"
export ANDROID_NDK_HOME="$ANDROID_HOME/ndk/27.3.13750724"
export JAVA_HOME="/c/Program Files/Android/Android Studio/jbr"

export PATH="$JAVA_HOME/bin:$ANDROID_HOME/platform-tools:$ANDROID_HOME/cmake/3.31.6/bin:$ANDROID_NDK_HOME:$PATH"

echo "Toolchain ready:"
command -v java   && java -version 2>&1 | head -1
command -v cmake  && cmake --version | head -1
command -v adb    && adb version | head -1
echo "ANDROID_HOME=$ANDROID_HOME"
echo "ANDROID_NDK_HOME=$ANDROID_NDK_HOME"
