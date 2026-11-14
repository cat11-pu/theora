/* Fuzz harness: feed a sequence of raw ogg_packets (length-prefixed) into the
 * Theora decoder. Input format:
 *   repeated: uint32 LE length, uint8 data[length]  (length 0 => EOS)
 * The harness never crashes on malformed input by design; any ASan/UBSan
 * violation aborts the process and is caught by the runner.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ogg/ogg.h>
#include <theora/theoradec.h>

static uint32_t rd_u32(const unsigned char **p, const unsigned char *end) {
  if ((size_t)(end - *p) < 4) return 0xFFFFFFFFu;
  uint32_t v = (uint32_t)(*p)[0] | ((uint32_t)(*p)[1] << 8) |
               ((uint32_t)(*p)[2] << 16) | ((uint32_t)(*p)[3] << 24);
  *p += 4;
  return v;
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: %s file\n", argv[0]); return 2; }
  FILE *f = fopen(argv[1], "rb");
  if (!f) { perror("fopen"); return 2; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  if (sz < 0 || sz > 64 * 1024 * 1024) { fclose(f); return 2; }
  unsigned char *buf = malloc((size_t)sz + 1);
  if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) { fclose(f); free(buf); return 2; }
  fclose(f);

  const unsigned char *p = buf, *end = buf + sz;
  th_info ti; th_comment tc; th_setup_info *ts = NULL;
  th_info_init(&ti); th_comment_init(&tc);

  int hdr_count = 0, ret = 0;
  ogg_packet op; memset(&op, 0, sizeof(op));
  int got_headers = 0;
  th_dec_ctx *dec = NULL;

  while (p < end) {
    uint32_t len = rd_u32(&p, end);
    if (len == 0xFFFFFFFFu) break;
    if (len > (uint32_t)(end - p)) break;
    unsigned char *data = (unsigned char *)p;
    p += len;
    op.packet = data; op.bytes = (long)len; op.b_o_s = (hdr_count == 0);
    op.e_o_s = (p >= end); op.granulepos = 0; op.packetno = hdr_count;

    if (!got_headers) {
      ret = th_decode_headerin(&ti, &tc, &ts, &op);
      if (ret < 0) {
        /* header rejected: keep feeding a few packets in case the caller
         * mislabeled boundaries, then give up. */
        hdr_count++;
        if (hdr_count > 8) break;
        continue;
      }
      hdr_count++;
      if (hdr_count == 3) {
        dec = th_decode_alloc(&ti, ts);
        if (dec != NULL) got_headers = 1;
        else break;
      }
      continue;
    }
    ret = th_decode_packetin(dec, &op, NULL);
    if (ret == 0) {
      th_ycbcr_buffer ycbcr;
      if (th_decode_ycbcr_out(dec, ycbcr) == 0) {
        /* touch every output plane to catch write-side corruption */
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

  if (dec) th_decode_free(dec);
  if (ts) th_setup_free(ts);
  th_comment_clear(&tc);
  th_info_clear(&ti);
  free(buf);
  return 0;
}
