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

# ---------------------------------------------------------------------------
# Wall-clock timing.
#
# NOTE(yigit): $SECONDS counts whole seconds, which is far too coarse for a
# build this size.  EPOCHREALTIME is a bash 5 builtin giving seconds.micro-
# seconds, and reading it is a parameter expansion - no process, no subshell,
# nothing forked.  Removing the dot turns it straight into microseconds.
#
# That matters more than it looks.  The obvious version, S=$(date +%s%N), forks
# twice per timer, and on a build that finishes in a third of a second the
# measurement was costing a measurable fraction of what it measured.  The
# fallback below is kept for a bash older than 5, where the fork is the only
# option.
#
# The two halves are timed separately on purpose.  The .so is what gets rebuilt
# every few minutes while the game is running, so that number is the iteration
# loop.  x_knyt only changes when the platform layer does.
# ---------------------------------------------------------------------------
if [ -n "${EPOCHREALTIME:-}" ]; then
    NowUs() { TimeNow=${EPOCHREALTIME/./}; }
else
    NowUs() { TimeNow=$(( $(date +%s%N) / 1000 )); }
fi

# Two microsecond stamps in, seconds with two decimals out.
Elapsed()
{
    local Ms=$(( ($2 - $1) / 1000 ))
    printf "%d.%02d" $(( Ms / 1000 )) $(( (Ms % 1000) / 10 ))
}

# --- Build the Game Library (.so) ---
# We compile to .new first, then move it. This makes the swap "atomic"
# so the game doesn't crash during a reload.
NowUs; GameStart=$TimeNow
g++ ${CPPSTD} ${WARNFLAGS} -shared -fPIC -o debug/libknyt.so.new ../handmade/code/handmade.cpp ${DEBUG_FLAGS} -lm || exit 1
mv -f debug/libknyt.so.new debug/libknyt.so
NowUs; GameEnd=$TimeNow

# --- Build the Platform Layer (Executable) ---
NowUs; PlatformStart=$TimeNow
g++ ${CPPSTD} ${WARNFLAGS} -o x_knyt ../handmade/code/x_knyt.cpp ../handmade/code/x_opengl.cpp \
    ${DEBUG_FLAGS} ${XLIB} || exit 1
NowUs; PlatformEnd=$TimeNow

popd > /dev/null

echo
echo "  game .so      $(Elapsed $GameStart $GameEnd)s"
echo "  platform      $(Elapsed $PlatformStart $PlatformEnd)s"
echo "  total         $(Elapsed $GameStart $PlatformEnd)s"
