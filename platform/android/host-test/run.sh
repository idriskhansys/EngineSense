#!/usr/bin/env sh
# Builds the real JNI bridge as a host shared library and drives it from a desktop JVM.
set -eu
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../../.."
OUT="${1:-$(mktemp -d)}"
JAVA_HOME="${JAVA_HOME:-$(dirname "$(dirname "$(readlink -f "$(command -v javac)")")")}"
INC_OS=linux; [ "$(uname)" = "Darwin" ] && INC_OS=darwin
EXT=so;      [ "$(uname)" = "Darwin" ] && EXT=dylib
c++ -std=c++17 -O1 -fPIC -shared -pthread \
  -I"$JAVA_HOME/include" -I"$JAVA_HOME/include/$INC_OS" -I"$ROOT/core/include" \
  "$ROOT/platform/android/app/src/main/cpp/enginesense_jni.cpp" "$ROOT"/core/src/*.cpp \
  -o "$OUT/libenginesense_jni.$EXT"
javac -d "$OUT" "$HERE/com/enginesense/EngineSenseNative.java"
java -Djava.library.path="$OUT" -cp "$OUT" com.enginesense.EngineSenseNative
