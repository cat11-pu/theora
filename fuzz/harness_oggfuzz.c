/* Fuzz harness: treat input as a raw Ogg bitstream and run the full
 * libogg demux -> libtheora decode pipeline. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <execinfo.h>
#include <unistd.h>
#include <ogg/ogg.h>
#include <theora/theoradec.h>

static void on_alarm(int sig) {
  (void)sig;
  void *frames[40];
  int n = backtrace(frames, 40);
  fprintf(stderr, "\n=== HANG BACKTRACE ===\n");
  backtrace_symbols_fd(frames, n, 2);
  _exit(77);
}

int main(int argc, char **argv) {
  if (argc >= 3) signal(SIGALRM, on_alarm), alarm((unsigned)atoi(argv[3]));
  if (argc < 2) return 2;

#if 0
#endif
  FILE *f = fopen(argv[1], "rb");
  if (!f) return 2;
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 0 || sz > 64 * 1024 * 1024) { fclose(f); return 2; }
  char *buf = malloc((size_t)sz + 1);
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(buf); return 2; }
  fclose(f);

  ogg_sync_state oy;
  ogg_sync_init(&oy);
  if (sz > 0) {
    char *ob = ogg_sync_buffer(&oy, (long)sz);
    memcpy(ob, buf, (size_t)sz);
    ogg_sync_wrote(&oy, (long)sz);
  }
  free(buf);

  ogg_stream_state os; int os_init = 0;
  ogg_page og; ogg_packet op;
  th_info ti; th_comment tc; th_setup_info *ts = NULL;
  th_info_init(&ti); th_comment_init(&tc);
  th_dec_ctx *dec = NULL;
  int headers_done = 0, pages = 0;

  while (ogg_sync_pageout(&oy, &og) == 1 && pages < 100000) {
    pages++;
    if (!os_init) {
      ogg_stream_init(&os, ogg_page_serialno(&og));
      os_init = 1;
    }
    if (ogg_stream_pagein(&os, &og) < 0) continue;
    while (ogg_stream_packetout(&os, &op) == 1) {
      if (!headers_done) {
        if (th_decode_headerin(&ti, &tc, &ts, &op) <= 0) {
          /* non-theora or bad header; keep trying */
          if (op.packetno > 8) goto out;
          continue;
        }
        if (op.packetno == 2) {
          dec = th_decode_alloc(&ti, ts);
          headers_done = (dec != NULL);
          if (!headers_done) goto out;
        }
      } else {
        if (th_decode_packetin(dec, &op, NULL) == 0) {
          th_ycbcr_buffer ycbcr;
          if (th_decode_ycbcr_out(dec, ycbcr) == 0) {
            for (int pl = 0; pl < 3; pl++) {
              volatile unsigned char sink = 0;
              for (int y = 0; y < ycbcr[pl].height; y++)
                for (int x = 0; x < ycbcr[pl].width; x++)
                  sink ^= ycbcr[pl].data[y * ycbcr[pl].stride + x];
              (void)sink;
            }
          }
        }
      }
    }
  }
out:
  if (dec) th_decode_free(dec);
  if (ts) th_setup_free(ts);
  th_comment_clear(&tc);
  th_info_clear(&ti);
  if (os_init) ogg_stream_clear(&os);
  ogg_sync_clear(&oy);
  return 0;
}
