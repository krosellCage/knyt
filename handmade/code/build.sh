#!/bin/bash

# NOTE(yigit): Everything resolves relative to this script, so the build works
# no matter what the current directory is.  The output tree is the same one
# build.bat uses - <repo>/build - so both platform layers share build/data.
cd "$(dirname "$0")"

mkdir -p ../../build/debug
mkdir -p ../../build/data

# Assets (shaders, textures) live in handmade/data and are copied to the build
# directory, which the game reads at runtime.  handmade/data is the source of
# truth - never edit the copy under build/data, it gets overwritten.
cp -u ../data/* ../../build/data/

pushd ../../build > /dev/null

# Clean up flags
CPPSTD="-std=gnu++11"
WARNFLAGS="-Wall -Wno-unused-variable -Wno-unused-but-set-variable -Wno-write-strings -Wno-unused-function \
-Wno-switch -Wno-sign-compare -Wno-format -Wno-return-type -Wno-int-to-pointer-cast -Wno-unknown-pragmas \
-Wno-missing-braces"

DEBUG_FLAGS="-ggdb -O0 -DHANDMADE_SLOW=1 -DHANDMADE_INTERNAL=1"

# NOTE(yigit): -lGL is what brings in GLX.  The X11 backbuffer used to be an
# XShm image, which is why -lXext was here; rendering goes through OpenGL now.
XLIB="-lX11 -lGL -lasound -ldl -lpthread -lm"

# --- Build the Game Library (.so) ---
# We compile to .new first, then move it. This makes the swap "atomic"
# so the game doesn't crash during a reload.
g++ ${CPPSTD} ${WARNFLAGS} -shared -fPIC -o debug/libknyt.so.new ../handmade/code/handmade.cpp ${DEBUG_FLAGS} -lm || exit 1
mv -f debug/libknyt.so.new debug/libknyt.so

# --- Build the Platform Layer (Executable) ---
g++ ${CPPSTD} ${WARNFLAGS} -o x_knyt ../handmade/code/x_knyt.cpp ../handmade/code/x_opengl.cpp \
    ${DEBUG_FLAGS} ${XLIB} || exit 1

popd > /dev/null
