/* Decode an Ogg Theora file and dump every decoded YCbCr frame in a
 * deterministic raw layout so two builds can be compared byte-for-byte.
 * Usage: dump <in.ogv> <out.raw>
 * Header: "THDUMP01" + uint32 width,height,frame_count then per frame:
 * per plane (Y,Cb,Cr): width,height,stride (u32) followed by row data
 * (width bytes per row, height rows). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ogg/ogg.h>
#include <theora/theoradec.h>

static void w32(FILE *f, uint32_t v) {
  unsigned char b[4] = {v & 255, (v >> 8) & 255, (v >> 16) & 255, v >> 24};
  fwrite(b, 1, 4, f);
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s in.ogv out.raw\n", argv[0]); return 2; }
  FILE *in = fopen(argv[1], "rb");
  if (!in) { perror("open in"); return 2; }
  FILE *out = fopen(argv[2], "wb");
  if (!out) { perror("open out"); return 2; }

  ogg_sync_state oy; ogg_sync_init(&oy);
  ogg_stream_state os; int os_init = 0;
  ogg_page og; ogg_packet op;
  th_info ti; th_comment tc; th_setup_info *ts = NULL;
  th_info_init(&ti); th_comment_init(&tc);
  th_dec_ctx *dec = NULL;
  int headers_done = 0;
  uint32_t frames = 0;
  long pos = ftell(out);
  fwrite("THDUMP01", 1, 8, out);
  w32(out, 0); w32(out, 0); w32(out, 0); /* patched later */

  char chunk[4096]; size_t n;
  while ((n = fread(chunk, 1, sizeof(chunk), in)) > 0) {
    char *ob = ogg_sync_buffer(&oy, (long)n);
    memcpy(ob, chunk, n);
    ogg_sync_wrote(&oy, (long)n);
    while (ogg_sync_pageout(&oy, &og) == 1) {
      if (!os_init) { ogg_stream_init(&os, ogg_page_serialno(&og)); os_init = 1; }
      if (ogg_stream_pagein(&os, &og) < 0) continue;
      while (ogg_stream_packetout(&os, &op) == 1) {
        if (!headers_done) {
          if (th_decode_headerin(&ti, &tc, &ts, &op) <= 0) continue;
          if (op.packetno == 2) {
            dec = th_decode_alloc(&ti, ts);
            if (!dec) { fprintf(stderr, "alloc failed\n"); return 1; }
            headers_done = 1;
          }
        } else {
          if (th_decode_packetin(dec, &op, NULL) == 0) {
            th_ycbcr_buffer y;
            if (th_decode_ycbcr_out(dec, y) != 0) continue;
            frames++;
            for (int pl = 0; pl < 3; pl++) {
              w32(out, (uint32_t)y[pl].width);
              w32(out, (uint32_t)y[pl].height);
              w32(out, (uint32_t)y[pl].stride);
              for (int row = 0; row < y[pl].height; row++)
                fwrite(y[pl].data + row * y[pl].stride, 1, y[pl].width, out);
            }
          }
        }
      }
    }
  }

  fseek(out, pos + 8, SEEK_SET);
  w32(out, (uint32_t)ti.pic_width);
  w32(out, (uint32_t)ti.pic_height);
  w32(out, frames);
  fclose(out); fclose(in);
  if (dec) th_decode_free(dec);
  if (ts) th_setup_free(ts);
  th_comment_clear(&tc); th_info_clear(&ti);
  if (os_init) ogg_stream_clear(&os);
  ogg_sync_clear(&oy);
  fprintf(stderr, "decoded %u frames (%ux%u)\n", frames,
          (unsigned)ti.pic_width, (unsigned)ti.pic_height);
  return frames ? 0 : 1;
}
