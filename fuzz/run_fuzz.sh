#!/bin/bash
# Run every malformed case through both harnesses under ASan+UBSan+LSan.
# Produces:
#   report/fuzz_results.tsv   per-case status with error category + offset
#   report/crashes/           sanitizer logs + minimized repro files
set -u
cd "$(dirname "$0")"
mkdir -p report/crashes
RES=report/fuzz_results.tsv
echo -e "case\tcontainer\tstatus\truntime_ms\tcategory\toffset\tmessage" > "$RES"

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=1:halt_on_error=1:symbolize=1:allocator_may_return_null=1"
export UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1:symbolize=1"

run_one() {
  local harness=$1 file=$2 case=$3 container=$4
  local log=/tmp/fuzzlog.$$
  local start end elapsed rc
  start=$(date +%s%N)
  timeout -s KILL 10s "$harness" "$file" >/dev/null 2>"$log"
  rc=$?
  end=$(date +%s%N)
  elapsed=$(( (end - start) / 1000000 ))
  if [ $rc -eq 0 ]; then
    echo -e "$case\t$container\trejected_ok\t$elapsed\t-\t-\t-" >> "$RES"
  elif [ $rc -eq 137 ] || [ $rc -eq 124 ]; then
    echo -e "$case\t$container\tTIMEOUT\t$elapsed\thang\t-\texceeded 10s" >> "$RES"
    cp "$file" "report/crashes/${case}_${container}.repro"
    cp "$log" "report/crashes/${case}_${container}.log"
  else
    # sanitizer crash: extract category and offending offset/address
    local cat msg addr
    cat=$(grep -oE "(ERROR: (AddressSanitizer|LeakSanitizer|UndefinedBehaviorSanitizer)[^ ]*|runtime error: [^ ]+)" "$log" | head -1 | cut -c1-60)
    msg=$(grep -E "SUMMARY:|runtime error:" "$log" | head -1 | tr '\t' ' ' | cut -c1-160)
    addr=$(grep -oE "0x[0-9a-f]+" "$log" | head -1)
    echo -e "$case\t$container\tCRASH\t$elapsed\t${cat:-unknown}\t${addr:-0}\t${msg:-see log}" >> "$RES"
    cp "$file" "report/crashes/${case}_${container}.repro"
    cp "$log" "report/crashes/${case}_${container}.log"
  fi
  rm -f "$log"
}

n=0
for f in corpus/raw/*; do
  case=$(basename "$f" .raw)
  run_one ./bin/fuzz_raw "$f" "$case" raw
  n=$((n+1))
done
for f in corpus/ogg/*; do
  case=$(basename "$f" .ogv)
  run_one ./bin/fuzz_ogg "$f" "$case" ogg
  n=$((n+1))
done

echo "ran $n executions"
echo "--- status counts ---"
cut -f3 "$RES" | sort | uniq -c
echo "--- crash categories ---"
awk -F'\t' '$3=="CRASH"{print $5}' "$RES" | sort | uniq -c
