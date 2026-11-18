/* Legal-stream pixel-exactness test.
 *
 * Encodes 50 legal frames with libtheora into an Ogg stream, decodes it
 * with libtheora, decodes the same file with the reference decoder
 * (ffmpeg's Theora decoder), and compares the two YUV420 outputs
 * pixel-by-pixel. Any mismatch is reported with frame/plane/x/y offsets
 * and the differing frames are dumped as counterexamples.
 */
#include "corpus.h"

#include <ogg/ogg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define NFRAMES 50
#define FRAME_BYTES (CORPUS_W * CORPUS_H * 3 / 2)

static int write_ogg(const char *path) {
  th_info ti;
  th_comment tc;
  th_enc_ctx *enc;
  ogg_stream_state os;
  ogg_packet hdr[3], op;
  ogg_page og;
  FILE *f;
  int i, ret = -1;

  th_info_init(&ti);
  ti.frame_width = CORPUS_W;
  ti.frame_height = CORPUS_H;
  ti.pic_width = CORPUS_W;
  ti.pic_height = CORPUS_H;
  ti.pic_x = 0;
  ti.pic_y = 0;
  ti.fps_numerator = 25;
  ti.fps_denominator = 1;
  ti.aspect_numerator = 1;
  ti.aspect_denominator = 1;
  ti.colorspace = TH_CS_UNSPECIFIED;
  ti.pixel_fmt = TH_PF_420;
  ti.quality = 32;
  ti.keyframe_granule_shift = 6;

  enc = th_encode_alloc(&ti);
  if (!enc) return -1;
  th_comment_init(&tc);
  f = fopen(path, "wb");
  if (!f) return -1;
  ogg_stream_init(&os, 0x1D2A3B4C);

  for (i = 0; i < 3; i++) {
    if (th_encode_flushheader(enc, &tc, &hdr[i]) <= 0) goto out;
    ogg_stream_packetin(&os, &hdr[i]);
  }
  while (ogg_stream_flush(&os, &og)) {
    fwrite(og.header, 1, og.header_len, f);
    fwrite(og.body, 1, og.body_len, f);
  }

  for (i = 0; i < NFRAMES; i++) {
    th_ycbcr_buffer yuv;
    unsigned char *planes[3];
    int p, w, h;
    for (p = 0; p < 3; p++) {
      w = p ? CORPUS_W / 2 : CORPUS_W;
      h = p ? CORPUS_H / 2 : CORPUS_H;
      planes[p] = malloc((size_t)w * h);
      yuv[p].data = planes[p];
      yuv[p].width = w;
      yuv[p].height = h;
      yuv[p].stride = w;
    }
    corpus_fill_frame(yuv, i);
    if (th_encode_ycbcr_in(enc, yuv) < 0) {
      for (p = 0; p < 3; p++) free(planes[p]);
      goto out;
    }
    for (p = 0; p < 3; p++) free(planes[p]);
    while (th_encode_packetout(enc, i == NFRAMES - 1, &op) > 0)
      ogg_stream_packetin(&os, &op);
    while (ogg_stream_pageout(&os, &og)) {
      fwrite(og.header, 1, og.header_len, f);
      fwrite(og.body, 1, og.body_len, f);
    }
  }
  while (ogg_stream_flush(&os, &og)) {
    fwrite(og.header, 1, og.header_len, f);
    fwrite(og.body, 1, og.body_len, f);
  }
  ret = 0;
out:
  ogg_stream_clear(&os);
  fclose(f);
  th_encode_free(enc);
  th_comment_clear(&tc);
  th_info_clear(&ti);
  return ret;
}

/* decode the ogg file with libtheora into contiguous yuv420p frames */
static int decode_libtheora(const char *path, unsigned char *out,
                            int max_frames) {
  FILE *f = fopen(path, "rb");
  ogg_sync_state oy;
  ogg_stream_state os;
  ogg_page og;
  ogg_packet op;
  th_info ti;
  th_comment tc;
  th_setup_info *setup = NULL;
  th_dec_ctx *dec = NULL;
  int os_open = 0, headers_done = 0, nframes = 0;

  if (!f) return -1;
  ogg_sync_init(&oy);
  th_info_init(&ti);
  th_comment_init(&tc);

  while (nframes < max_frames) {
    char *buf = ogg_sync_buffer(&oy, 65536);
    long n = fread(buf, 1, 65536, f);
    if (n < 0) break;
    ogg_sync_wrote(&oy, n);
    while (ogg_sync_pageout(&oy, &og) == 1) {
      if (!os_open) {
        ogg_stream_init(&os, ogg_page_serialno(&og));
        os_open = 1;
      }
      ogg_stream_pagein(&os, &og);
      while (ogg_stream_packetout(&os, &op) == 1) {
        if (!headers_done) {
          int r = th_decode_headerin(&ti, &tc, &setup, &op);
          if (r < 0) goto done;
          if (r > 0) continue; /* header consumed, more expected */
          /* r == 0: op is the first data packet; decode it below */
          headers_done = 1;
          dec = th_decode_alloc(&ti, setup);
          if (!dec) goto done;
        }
        ogg_int64_t gp = 0;
        if (th_decode_packetin(dec, &op, &gp) == 0) {
          th_ycbcr_buffer yuv;
          if (th_decode_ycbcr_out(dec, yuv) == 0) {
            unsigned char *dst = out + (size_t)nframes * FRAME_BYTES;
            int p, y;
            for (p = 0; p < 3; p++) {
              for (y = 0; y < yuv[p].height; y++) {
                memcpy(dst, yuv[p].data + (ptrdiff_t)y * yuv[p].stride,
                       (size_t)yuv[p].width);
                dst += yuv[p].width;
              }
            }
            nframes++;
            if (nframes >= max_frames) break;
          }
        }
      }
      if (nframes >= max_frames) break;
    }
    if (n == 0) break; /* EOF: pages drained above */
  }
done:
  if (dec) th_decode_free(dec);
  th_setup_free(setup);
  if (os_open) ogg_stream_clear(&os);
  ogg_sync_clear(&oy);
  th_comment_clear(&tc);
  th_info_clear(&ti);
  fclose(f);
  return nframes;
}

int main(int argc, char **argv) {
  const char *dir = argc > 1 ? argv[1] : ".";
  char ogv[512], ffyuv[512], cmd[1200], sumpath[512];
  unsigned char *lib = NULL, *ff = NULL;
  FILE *f = NULL, *summary = NULL;
  int nlib, nff, frame, mismatches = 0, maxdiff = 0;
  int ret = 1;

  snprintf(ogv, sizeof(ogv), "%s/legal50.ogv", dir);
  snprintf(ffyuv, sizeof(ffyuv), "%s/legal50_ff.yuv", dir);
  snprintf(sumpath, sizeof(sumpath), "%s/pixel_summary.txt", dir);

  if (write_ogg(ogv) < 0) {
    fprintf(stderr, "encode/mux failed\n");
    return 2;
  }

  lib = malloc((size_t)NFRAMES * FRAME_BYTES);
  ff = malloc((size_t)NFRAMES * FRAME_BYTES);
  nlib = decode_libtheora(ogv, lib, NFRAMES);

  /* reference decode with ffmpeg */
  snprintf(cmd, sizeof(cmd),
           "ffmpeg -v error -y -i '%s' -f rawvideo -pix_fmt yuv420p '%s'",
           ogv, ffyuv);
  if (system(cmd) != 0) {
    fprintf(stderr, "ffmpeg reference decode failed\n");
    goto out;
  }
  f = fopen(ffyuv, "rb");
  if (!f) goto out;
  nff = (int)fread(ff, FRAME_BYTES, NFRAMES, f);
  fclose(f);
  f = NULL;

  summary = fopen(sumpath, "w");
  fprintf(stderr, "libtheora frames=%d ffmpeg frames=%d\n", nlib, nff);
  fprintf(summary, "frames libtheora=%d ffmpeg=%d\n", nlib, nff);
  if (nlib != NFRAMES || nff != NFRAMES) {
    fprintf(summary, "FAIL: frame count mismatch (expected %d)\n", NFRAMES);
    goto out;
  }

  for (frame = 0; frame < NFRAMES; frame++) {
    unsigned char *a = lib + (size_t)frame * FRAME_BYTES;
    unsigned char *b = ff + (size_t)frame * FRAME_BYTES;
    int i;
    for (i = 0; i < FRAME_BYTES; i++) {
      if (a[i] != b[i]) {
        int plane, x, y, w;
        if (i < CORPUS_W * CORPUS_H) {
          plane = 0; w = CORPUS_W;
          y = i / w; x = i % w;
        } else if (i < CORPUS_W * CORPUS_H + CORPUS_W * CORPUS_H / 4) {
          plane = 1; w = CORPUS_W / 2;
          y = (i - CORPUS_W * CORPUS_H) / w;
          x = (i - CORPUS_W * CORPUS_H) % w;
        } else {
          plane = 2; w = CORPUS_W / 2;
          y = (i - CORPUS_W * CORPUS_H - CORPUS_W * CORPUS_H / 4) / w;
          x = (i - CORPUS_W * CORPUS_H - CORPUS_W * CORPUS_H / 4) % w;
        }
        if (mismatches < 64)
          fprintf(summary,
                  "MISMATCH frame=%d plane=%c x=%d y=%d lib=%d ref=%d\n",
                  frame, "YUV"[plane], x, y, a[i], b[i]);
        mismatches++;
        {
          int d = abs(a[i] - b[i]);
          if (d > maxdiff) maxdiff = d;
        }
      }
    }
  }
  fprintf(summary, "pixels compared=%d mismatches=%d max_abs_diff=%d -> %s\n",
          NFRAMES * FRAME_BYTES, mismatches, maxdiff,
          mismatches == 0 ? "PIXEL-EXACT MATCH" : "MISMATCH");
  fprintf(stderr, "mismatches=%d max_abs_diff=%d\n", mismatches, maxdiff);
  ret = mismatches ? 1 : 0;

out:
  if (summary) fclose(summary);
  free(lib);
  free(ff);
  return ret;
}
