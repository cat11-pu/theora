#!/bin/sh
# Build and run the decoder robustness + pixel-exactness tests.
# Usage: tests/robustness/run.sh [artifacts_dir]
set -e

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
BUILD="$ROOT/build-asan"
ART=${1:-"$ROOT/artifacts"}
SRC="$ROOT/tests/robustness"
CEX="$ART/counterexamples"
mkdir -p "$CEX"

SAN="-g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
INC="-I$ROOT/include $(pkg-config --cflags ogg)"
LIBS="$BUILD/lib/.libs/libtheoraenc.a $BUILD/lib/.libs/libtheoradec.a $(pkg-config --libs ogg) -lm"

echo "== building tests =="
gcc $SAN $INC -o "$ART/malformed_test" "$SRC/malformed_test.c" "$SRC/corpus.c" $LIBS
gcc $SAN $INC -o "$ART/pixel_compare" "$SRC/pixel_compare.c" "$SRC/corpus.c" $LIBS

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0:print_stacktrace=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:suppressions=$SRC/ubsan.supp"

echo "== malformed-frame property test (200 cases) =="
"$ART/malformed_test" "$CEX" "$ART/malformed_summary.txt"

echo "== legal-stream pixel-exactness test (50 frames) =="
"$ART/pixel_compare" "$ART"

echo "== done; summaries in $ART =="
