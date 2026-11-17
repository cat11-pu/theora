#!/bin/bash
# Dumps every frame of a stream with two decoder binaries and compares the
# raw YUV planes byte-for-byte (per-pixel exactness check).
# Usage: diff_dump.sh <dec_bin_a> <dec_bin_b> <stream.ogv>
set -e
DEC_A="$1"
DEC_B="$2"
STREAM="$3"
TMPA=$(mktemp -d)
TMPB=$(mktemp -d)
"$DEC_A" "$STREAM" "$TMPA/f" >/dev/null
"$DEC_B" "$STREAM" "$TMPB/f" >/dev/null
n_a=$(ls "$TMPA" | wc -l)
n_b=$(ls "$TMPB" | wc -l)
if [ "$n_a" != "$n_b" ]; then
  echo "FRAME COUNT MISMATCH: $n_a vs $n_b"
  exit 1
fi
bad=0
for f in "$TMPA"/*.yuv; do
  base=$(basename "$f")
  if ! cmp -s "$f" "$TMPB/$base"; then
    echo "DIFF in $base"
    bad=1
  fi
done
if [ "$bad" = 0 ]; then
  echo "EXACT: $n_a frames, all pixels identical"
fi
rm -rf "$TMPA" "$TMPB"
exit $bad
