/* Diagnostic harness: decode a malformed stream and report exactly where and
 * why the decoder rejected it.
 * usage: harness_diag <raw|ogg> <file>
 * Output (single line, tab separated):
 *   stage=<header|frame> packet=<n> pkt_offset=<bytes> stream_offset=<bytes>
 *   error=<code>
 * stream_offset is the absolute byte offset in the input container.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ogg/ogg.h>
#include <theora/theoradec.h>

static const char *errname(int e) {
  switch (e) {
    case TH_EFAULT: return "TH_EFAULT";
    case TH_EINVAL: return "TH_EINVAL";
    case TH_EBADHEADER: return "TH_EBADHEADER";
    case TH_EBADPACKET: return "TH_EBADPACKET";
    case TH_EVERSION: return "TH_EVERSION";
    case TH_EIMPL: return "TH_EIMPL";
    case TH_ENOTFORMAT: return "TH_ENOTFORMAT";
    case TH_DUPFRAME: return "TH_DUPFRAME";
    case 0: return "OK";
    default: return "UNKNOWN";
  }
}

static int run_packets(ogg_packet *ops, size_t npkt, size_t *pkt_lens) {
  th_info ti; th_comment tc; th_setup_info *ts = NULL;
  th_info_init(&ti); th_comment_init(&tc);
  th_dec_ctx *dec = NULL;
  size_t stream_off = 0;
  int headers = 0;
  for (size_t i = 0; i < npkt; i++) {
    if (headers < 3) {
      int r = th_decode_headerin(&ti, &tc, &ts, &ops[i]);
      if (r < 0) {
        fprintf(stdout, "stage=header packet=%zu pkt_offset=0 stream_offset=%zu error=%s(%d) dims=%ux%u\n",
                i, stream_off, errname(r), r, ti.frame_width, ti.frame_height);
        if (ts) th_setup_free(ts);
        th_comment_clear(&tc); th_info_clear(&ti);
        return 0;
      }
      /*The three headers return 3, 2, 1; the first data packet then returns
         zero.  Counting the positive header returns avoids consuming a data
         packet inside the header phase.*/
      headers += (r > 0);
      if (headers == 3) {
        dec = th_decode_alloc(&ti, ts);
        if (!dec) {
          fprintf(stdout, "stage=alloc packet=%zu stream_offset=%zu error=ALLOC_FAIL dims=%ux%u\n",
                  i, stream_off, ti.frame_width, ti.frame_height);
          th_setup_free(ts); th_comment_clear(&tc); th_info_clear(&ti);
          return 0;
        }
      }
    } else {
      int r = th_decode_packetin(dec, &ops[i], NULL);
      if (r < 0) {
        fprintf(stdout, "stage=frame packet=%zu pkt_offset=0 stream_offset=%zu error=%s(%d)\n",
                i, stream_off, errname(r), r);
        th_decode_free(dec);
        if (ts) th_setup_free(ts);
        th_comment_clear(&tc); th_info_clear(&ti);
        return 0;
      }
    }
    stream_off += pkt_lens[i];
  }
  fprintf(stdout, "stage=none packet=- stream_offset=%zu error=ACCEPTED(decoder consumed all packets)\n",
          stream_off);
  if (dec) th_decode_free(dec);
  if (ts) th_setup_free(ts);
  th_comment_clear(&tc); th_info_clear(&ti);
  return 0;
}

static uint32_t rd32(const unsigned char **p, const unsigned char *e) {
  if ((size_t)(e - *p) < 4) return 0xFFFFFFFFu;
  uint32_t v = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8) |
               ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
  *p += 4; return v;
}

int main(int argc, char **argv) {
  if (argc < 3) { fprintf(stderr, "usage: %s raw|ogg file\n", argv[0]); return 2; }
  FILE *f = fopen(argv[2], "rb");
  if (!f) return 2;
  fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
  unsigned char *buf = malloc((size_t)sz + 1);
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); return 2; }
  fclose(f);

  ogg_packet ops[256];
  size_t lens[256], n = 0;
  unsigned char *store[256];
  memset(ops, 0, sizeof(ops));
  memset(store, 0, sizeof(store));

  if (!strcmp(argv[1], "raw")) {
    const unsigned char *p = buf, *end = buf + sz;
    while (p < end && n < 256) {
      uint32_t len = rd32(&p, end);
      if (len == 0xFFFFFFFFu || len > (uint32_t)(end - p)) break;
      store[n] = malloc(len ? len : 1);
      memcpy(store[n], p, len);
      ops[n].packet = store[n];
      ops[n].bytes = (long)len;
      ops[n].b_o_s = (n == 0);
      ops[n].packetno = (long)n;
      lens[n] = len + 4;
      p += len; n++;
    }
  } else {
    ogg_sync_state oy; ogg_sync_init(&oy);
    if (sz > 0) {
      memcpy(ogg_sync_buffer(&oy, sz), buf, (size_t)sz);
      ogg_sync_wrote(&oy, sz);
    }
    ogg_page og; ogg_stream_state os; int ini = 0;
    while (ogg_sync_pageout(&oy, &og) == 1 && n < 256) {
      if (!ini) { ogg_stream_init(&os, ogg_page_serialno(&og)); ini = 1; }
      if (ogg_stream_pagein(&os, &og) < 0) continue;
      while (n < 255 && ogg_stream_packetout(&os, &ops[n]) == 1) {
        long plen = ops[n].bytes;
        store[n] = malloc(plen ? (size_t)plen : 1);
        memcpy(store[n], ops[n].packet, (size_t)plen);
        ops[n].packet = store[n];
        lens[n] = (size_t)plen; n++;
      }
    }
    if (ini) ogg_stream_clear(&os);
    ogg_sync_clear(&oy);
  }
  int rc = run_packets(ops, n, lens);
  for (size_t i = 0; i < n; i++) free(store[i]);
  free(buf);
  return rc;
}
