#!/bin/bash
# Produce a per-case rejection report (stage / packet / stream offset / code).
set -u
cd "$(dirname "$0")"
OUT=report/rejection_map.tsv
echo -e "case\tcontainer\tstage\tpacket\tstream_offset\terror" > "$OUT"
for mode in raw ogg; do
  for f in corpus/$mode/*; do
    case=$(basename "$f")
    line=$(./bin/fuzz_diag "$mode" "$f" 2>/dev/null)
    stage=$(echo "$line" | sed -n 's/.*stage=\([^ ]*\).*/\1/p')
    pkt=$(echo "$line" | sed -n 's/.*packet=\(-\?[0-9]*\).*/\1/p')
    off=$(echo "$line" | sed -n 's/.*stream_offset=\([0-9]*\).*/\1/p')
    err=$(echo "$line" | sed -n 's/.*error=\([^ ]*\).*/\1/p')
    echo -e "$case\t$mode\t$stage\t$pkt\t$off\t$err" >> "$OUT"
  done
done
echo "mapped $(($(wc -l < "$OUT") - 1)) cases"
echo "--- error distribution ---"
cut -f6 "$OUT" | sort | uniq -c | sort -rn
echo "--- stage distribution ---"
cut -f3 "$OUT" | sort | uniq -c
