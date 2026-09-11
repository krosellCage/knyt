#!/bin/bash

# NOTE(yigit): Everything resolves relative to this script, so the build works
# no matter what the current directory is.  The output tree is the same one
# build.bat uses - <repo>/build - so both platform layers share build/data.
cd "$(dirname "$0")"

mkdir -p ../../build/debug

# Assets are NOT copied.  build/data is a symlink to handmade/data, so there is
# exactly one copy of every asset on disk and an edit is live immediately -
# there is no "source of truth" left to get wrong, because there is only one
# file.  The Windows build makes the same path a directory junction.
#
# NOTE(yigit): The target is relative to the LINK, not to this script, which is
# why it is ../handmade/data and not ../data.
#
# rm -rf ../../build removes the symlink itself, not what it points at.
if [ ! -e ../../build/data ]; then
    ln -s ../handmade/data ../../build/data
fi

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
