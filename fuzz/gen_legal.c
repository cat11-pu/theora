/* Generate a legal Ogg Theora stream with deterministic synthetic content.
 * usage: gen_legal <out.ogv> <width> <height> <fps_num> <fps_den>
 *                  <quality> <frames> <keyframe_interval> <pixel_fmt> <seed>
 * pixel_fmt: 0=420, 1=422, 2=444
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ogg/ogg.h>
#include <theora/theoraenc.h>

static unsigned rng_state;
static unsigned rnd(void) {
  rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17;
  rng_state ^= rng_state << 5; return rng_state;
}

static void w_page(ogg_stream_state *os, ogg_page *og, FILE *f) {
  (void)os;
  fwrite(og->header, 1, og->header_len, f);
  fwrite(og->body, 1, og->body_len, f);
}

int main(int argc, char **argv) {
  if (argc < 11) { fprintf(stderr, "bad args\n"); return 2; }
  const char *path = argv[1];
  th_info ti; th_info_init(&ti);
  ti.frame_width = (atoi(argv[2]) + 15) & ~15;
  ti.frame_height = (atoi(argv[3]) + 15) & ~15;
  ti.pic_width = atoi(argv[2]);
  ti.pic_height = atoi(argv[3]);
  ti.pic_x = ti.pic_y = 0;
  ti.fps_numerator = atoi(argv[4]);
  ti.fps_denominator = atoi(argv[5]);
  ti.aspect_numerator = ti.aspect_denominator = 1;
  ti.colorspace = TH_CS_UNSPECIFIED;
  static const int pfm[] = {TH_PF_420, TH_PF_422, TH_PF_444};
  ti.pixel_fmt = pfm[atoi(argv[9]) % 3];
  ti.target_bitrate = 0;
  ti.quality = atoi(argv[6]);
  ti.keyframe_granule_shift = 6;
  int nframes = atoi(argv[7]);
  rng_state = (unsigned)atoi(argv[10]);
  if (rng_state == 0) rng_state = 0x12345678;

  th_comment tc; th_comment_init(&tc);
  th_enc_ctx *enc = th_encode_alloc(&ti);
  if (!enc) { fprintf(stderr, "encode alloc failed\n"); return 1; }
  int ki = atoi(argv[8]);
  if (ki > 0) th_encode_ctl(enc, TH_ENCCTL_SET_KEYFRAME_FREQUENCY_FORCE, &ki, sizeof(ki));

  FILE *f = fopen(path, "wb");
  if (!f) { perror("out"); return 1; }
  ogg_stream_state os;
  ogg_stream_init(&os, (int)(rnd() & 0x7fffffff));
  ogg_packet op;
  int hdr = 0;
  while (th_encode_flushheader(enc, &tc, &op) > 0) {
    ogg_stream_packetin(&os, &op);
    ogg_page og;
    while (ogg_stream_flush(&os, &og)) w_page(&os, &og, f);
    hdr++;
  }
  if (hdr != 3) { fprintf(stderr, "header count %d\n", hdr); return 1; }

  int yw = ti.frame_width, yh = ti.frame_height;
  int cx = (ti.pixel_fmt == TH_PF_444) ? yw : (yw + (ti.pixel_fmt == TH_PF_420 ? 0 : 0));
  int cw = yw, ch = yh;
  if (ti.pixel_fmt == TH_PF_420) { cw = yw / 2; ch = yh / 2; }
  if (ti.pixel_fmt == TH_PF_422) { cw = yw / 2; ch = yh; }
  (void)cx;
  th_ycbcr_buffer ycb;
  unsigned char *Y = malloc((size_t)yw * yh);
  unsigned char *Cb = malloc((size_t)cw * ch);
  unsigned char *Cr = malloc((size_t)cw * ch);
  ycb[0].width = yw; ycb[0].height = yh; ycb[0].stride = yw; ycb[0].data = Y;
  ycb[1].width = cw; ycb[1].height = ch; ycb[1].stride = cw; ycb[1].data = Cb;
  ycb[2].width = cw; ycb[2].height = ch; ycb[2].stride = cw; ycb[2].data = Cr;

  for (int fr = 0; fr < nframes; fr++) {
    for (int y = 0; y < yh; y++)
      for (int x = 0; x < yw; x++) {
        int v = (x * 7 + y * 13 + fr * 29 + (int)(rnd() & 31)) & 255;
        Y[y * yw + x] = (unsigned char)v;
      }
    for (int i = 0; i < cw * ch; i++) {
      Cb[i] = (unsigned char)((rnd() + fr * 11) & 255);
      Cr[i] = (unsigned char)((rnd() ^ (fr * 7)) & 255);
    }
    if (th_encode_ycbcr_in(enc, ycb) < 0) { fprintf(stderr, "encode in fail\n"); return 1; }
    ogg_packet p;
    while (th_encode_packetout(enc, fr == nframes - 1, &p) > 0) {
      ogg_stream_packetin(&os, &p);
      ogg_page og;
      while (ogg_stream_pageout(&os, &og)) w_page(&os, &og, f);
    }
  }
  ogg_page og;
  while (ogg_stream_flush(&os, &og)) w_page(&os, &og, f);

  free(Y); free(Cb); free(Cr);
  ogg_stream_clear(&os);
  th_encode_free(enc);
  th_comment_clear(&tc);
  th_info_clear(&ti);
  fclose(f);
  return 0;
}
