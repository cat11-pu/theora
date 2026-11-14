# Reference-frame loss handling

This directory contains a self-contained regression test for decoder
behavior when the reference frame required by an inter (P) frame is missing.

## What was wrong

`th_decode_packetin()` used to treat a missing reference as a recoverable,
normal condition:

- A 0-byte packet (the Ogg/Theora signal for a dropped frame) was decoded as
  an inter frame with no coded blocks and returned `TH_DUPFRAME`, silently
  replaying the previous output frame.
- If an inter frame arrived before any key frame, or after a lost frame,
  `oc_dec_init_dummy_frame()` synthesized a solid-gray reference so decoding
  could continue. The resulting pictures are unrelated to the real content,
  i.e. visible garbage/frozen output, while the caller received a success
  code and had no way to tell that the frame was invalid.

There was no error path that told the application "this frame cannot be
decoded because its reference is unavailable".

## The fix

The decoder now treats a missing required reference as an explicit error and
skips the frame instead of inventing picture data.

- A 0-byte/dropped packet immediately returns `TH_EBADPACKET` and invalidates
  the current reference frame indices (`lib/decode.c`,
  `oc_dec_mark_ref_missing()`).
- Every subsequent inter frame whose GOLD or PREV reference index is invalid
  is rejected with `TH_EBADPACKET` before any reconstruction or border/motion
  code can touch the (now NULL) reference pointers. No buffer is read out of
  bounds and no gray dummy frame is created.
- An intra (key) frame needs no references. It clears the loss state, rebuilds
  both GOLD and PREV from the reconstructed frame, and decoding resumes
  normally. A burst of any number of lost frames (the test uses ten) therefore
  resynchronizes exactly at the next key frame.
- The legacy wrapper `theora_decode_packetin()` maps the negative return to
  `OC_BADPACKET` automatically.

## Reproduction / test stream

`refloss_test.c` synthesizes the bitstream in-process with the library's own
encoder, so no binary fixture is checked in:

- 64x48 4:2:0, one key frame every 16 frames, 48 frames total.
- Scenario 1: the first frame (key) decodes, then ten consecutive frames are
  replaced by 0-byte dropped packets. The ten gaps and every inter frame up
  to the next key frame must return `TH_EBADPACKET`; the next key frame and
  all following frames must decode and produce valid output.
- Scenario 2: the decoder is fed inter frames with no preceding key frame
  (cold start). They must be rejected cleanly until the first key frame
  resynchronizes it.

## Running

```sh
# from the build tree
make check TESTS=refloss_test

# or directly
gcc -Iinclude -I. tests/refloss/refloss_test.c \
  lib/.libs/libtheora.a $(pkg-config --libs ogg) -lm -o refloss_test
./refloss_test
```

Expected final line: `ALL REFERENCE-LOSS TESTS PASSED`.

Before the fix the first scenario fails at the initial dropped packet with
`missing reference frame was not rejected`, because the old decoder returned
`TH_DUPFRAME` instead of an error code.
