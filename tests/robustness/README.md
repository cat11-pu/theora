# Decoder robustness & pixel-exactness tests

Two test programs, built against a sanitizer build of libtheora
(`build-asan/`, configured with `-fsanitize=address,undefined`).

## malformed_test — 200 malformed packets, 4 categories (50 each)

- `TRUNC` — frame packets truncated at a random offset
- `BADQUANT` — setup header (quant matrices / Huffman tables) corrupted
- `BIGMV` — motion-vector window of inter frames corrupted (0xFF runs)
- `HDRLOOP` — header packets re-injected between data packets

Properties checked per case: zero crashes, zero out-of-bounds
(ASan/UBSan), zero leaks (LSan). Each case runs in a forked child so a
crash is attributable; the metadata line (with byte offsets) is flushed
before the case runs, and the mutated packet sequence is written to
`artifacts/counterexamples/case_NNN_<CAT>.seq` (8-byte LE length +
payload per packet) for offline reproduction.

## pixel_compare — 50 legal frames vs reference decoder

Encodes 50 frames with libtheora into `artifacts/legal50.ogv`, decodes
with libtheora and with ffmpeg's Theora decoder (reference), and
compares all Y/U/V samples pixel-by-pixel, reporting mismatches with
frame/plane/x/y offsets.

## Running

```sh
tests/robustness/run.sh            # builds + runs everything
```

Summaries land in `artifacts/malformed_summary.txt` and
`artifacts/pixel_summary.txt`.

`ubsan.supp` suppresses known-benign encoder UB (left-shift of negative
values in `lib/analyze.c`, `lib/mcenc.c`, `lib/tokenize.c`) that is only
hit while *generating* the corpus; decoder paths are not suppressed.
