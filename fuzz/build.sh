#!/bin/bash
# Build libtheora decoder with ASan+UBSan (pure C, no arch asm for clean stack traces)
set -e
cd "$(dirname "$0")"
ROOT=..
SRC="$ROOT/lib"
INC="$ROOT/include"
mkdir -p obj bin

DEC_SOURCES="apiwrapper bitpack decapiwrapper decinfo decode dequant fragment huffdec idct info internal quant state"

CC=${CC:-gcc}
SAN="-fsanitize=address,undefined -fno-omit-frame-pointer -g -O1"
CFLAGS="-std=c99 -I$INC -I$SRC $SAN -Wall -Wextra -Wno-unused-parameter -D_FORTIFY_SOURCE=0 -fno-sanitize-recover=all -DHAVE_MEMORY_CONSTRAINT"

for name in $DEC_SOURCES; do
  $CC $CFLAGS -c "$SRC/$name.c" -o "obj/$name.o"
done
ar rcs bin/libtheoradec_san.a obj/*.o
echo "static lib built: bin/libtheoradec_san.a"

# Harness 1: raw packet-sequence fuzzer (length-prefixed packets)
$CC $CFLAGS harness_fuzz.c -Lbin -ltheoradec_san -logg -o bin/fuzz_raw
# Harness 2: ogg container demuxer fuzzer
$CC $CFLAGS harness_oggfuzz.c -Lbin -ltheoradec_san -logg -o bin/fuzz_ogg
# Diagnostic harness: reports rejection packet/offset/error code
$CC $CFLAGS harness_diag.c -Lbin -ltheoradec_san -logg -o bin/fuzz_diag
# Harness 3: legal-stream decoder -> dump raw YCbCr frames
$CC $CFLAGS harness_dump.c -Lbin -ltheoradec_san -logg -o bin/dump_san
# Reference decoder dump (system libtheora 1.2.0, no sanitizer)
gcc -std=c99 -I$INC -I$SRC -g -O2 harness_dump.c -o bin/dump_ref \
  /usr/lib/x86_64-linux-gnu/libtheoradec.so.2 -logg
# Generator for legal streams (encoder via system libtheoraenc)
gcc -std=c99 -I$INC -I$SRC -g -O2 gen_legal.c -o bin/gen_legal \
  /usr/lib/x86_64-linux-gnu/libtheoraenc.so.2 \
  /usr/lib/x86_64-linux-gnu/libtheoradec.so.2 -logg -lm
ls -la bin/
