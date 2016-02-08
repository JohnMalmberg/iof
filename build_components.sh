#!/bin/sh

set -e
set -x

option=
if [ -n "$PREBUILT_AREA" ]; then
export option="TARGET_PREFIX=${PREBUILT_AREA}"
fi

scons $option SRC_PREFIX=../
scons $option SRC_PREFIX=../ install
