#!/bin/bash
# Generate 200 legal Theora streams across a parameter matrix and compare
# every decoded pixel against the system reference libtheora 1.2.0.
set -u
cd "$(dirname "$0")"
mkdir -p ref/streams ref/san ref/ref
LOG=report/legal_results.tsv
echo -e "id\twidth\theight\tfps\tquality\tframes\tpf\tkeyint\tseed\tstatus\tmismatch_bytes" > "$LOG"

# width/height matrix (must be representable; encoder rounds up to /16)
W=(16 32 48 64 96 128 160 176 240 320)
H=(16 32 48 64 80 96 128 144 192 240)
FPS=("30:1" "25:1" "24000:1001" "15:2")
PF=(0 1 2)

id=0
while [ $id -lt 200 ]; do
  w=${W[$((id % 10))]}
  h=${H[$(((id / 10) % 10))]}
  fps=${FPS[$(((id / 4) % 4))]}
  fn=${fps%:*}; fd=${fps#*:}
  q=$(( (id * 7) % 64 ))
  pf=${PF[$((id % 3))]}
  frames=$(( 3 + (id % 12) ))
  keyint=$(( 16 + (id % 4) * 32 ))
  seed=$(( 7000 + id ))
  s=ref/streams/legal_$(printf %03d $id).ogv
  ./bin/gen_legal "$s" "$w" "$h" "$fn" "$fd" "$q" "$frames" "$keyint" "$pf" "$seed" 2>/dev/null
  if [ ! -s "$s" ]; then
    echo -e "$id\t$w\t$h\t$fps\t$q\t$frames\t$pf\t$keyint\t$seed\tGEN_FAIL\t-" >> "$LOG"
    id=$((id+1)); continue
  fi
  san=ref/san/$(printf %03d $id).raw
  rfr=ref/ref/$(printf %03d $id).raw
  ./bin/dump_san "$s" "$san" 2>/dev/null
  ./bin/dump_ref "$s" "$rfr" 2>/dev/null
  if [ ! -s "$san" ] || [ ! -s "$rfr" ]; then
    echo -e "$id\t$w\t$h\t$fps\t$q\t$frames\t$pf\t$keyint\t$seed\tDECODE_FAIL\t-" >> "$LOG"
  elif cmp -s "$san" "$rfr"; then
    echo -e "$id\t$w\t$h\t$fps\t$q\t$frames\t$pf\t$keyint\t$seed\tPIXEL_MATCH\t0" >> "$LOG"
  else
    mb=$(cmp -l "$san" "$rfr" 2>/dev/null | wc -l)
    echo -e "$id\t$w\t$h\t$fps\t$q\t$frames\t$pf\t$keyint\t$seed\tMISMATCH\t$mb" >> "$LOG"
  fi
  id=$((id+1))
done

echo "generated+checked $id legal streams"
cut -f10 "$LOG" | sort | uniq -c
