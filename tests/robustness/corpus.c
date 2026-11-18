#include "corpus.h"

#include <stdlib.h>
#include <string.h>

void corpus_fill_frame(th_ycbcr_buffer yuv, int frame_no) {
  int x, y;
  for (y = 0; y < CORPUS_H; y++) {
    unsigned char *row = yuv[0].data + (ptrdiff_t)y * yuv[0].stride;
    for (x = 0; x < CORPUS_W; x++) {
      row[x] = (unsigned char)((x * 2 + y + frame_no * 13) & 0xFF);
    }
  }
  for (y = 0; y < CORPUS_H / 2; y++) {
    unsigned char *urow = yuv[1].data + (ptrdiff_t)y * yuv[1].stride;
    unsigned char *vrow = yuv[2].data + (ptrdiff_t)y * yuv[2].stride;
    for (x = 0; x < CORPUS_W / 2; x++) {
      urow[x] = (unsigned char)(128 + ((x + frame_no * 3) & 0x1F) - 16);
      vrow[x] = (unsigned char)(128 + ((y + frame_no * 5) & 0x1F) - 16);
    }
  }
}

static int pkt_copy(pkt_t *dst, const ogg_packet *src, int is_header) {
  dst->data = malloc(src->bytes > 0 ? (size_t)src->bytes : 1);
  if (!dst->data) return -1;
  memcpy(dst->data, src->packet, (size_t)src->bytes);
  dst->len = src->bytes;
  dst->is_header = is_header;
  dst->is_keyframe = !is_header && src->bytes > 0 &&
                     !(src->packet[0] & 0x80) && !(src->packet[0] & 0x40);
  return 0;
}

int corpus_generate(corpus_t *c) {
  th_info ti;
  th_comment tc;
  th_enc_ctx *enc;
  ogg_packet op;
  ogg_packet hdr[3];
  int i;

  memset(c, 0, sizeof(*c));
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
  ti.target_bitrate = 0;
  ti.keyframe_granule_shift = 6;

  enc = th_encode_alloc(&ti);
  if (!enc) return -1;
  c->info = ti;

  th_comment_init(&tc);
  for (i = 0; i < 3; i++) {
    if (th_encode_flushheader(enc, &tc, &hdr[i]) <= 0) {
      th_encode_free(enc);
      return -1;
    }
    if (pkt_copy(&c->headers[i], &hdr[i], 1) < 0) {
      th_encode_free(enc);
      return -1;
    }
  }

  for (i = 0; i < CORPUS_NFRAMES; i++) {
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
      th_encode_free(enc);
      return -1;
    }
    for (p = 0; p < 3; p++) free(planes[p]);
    while (th_encode_packetout(enc, i == CORPUS_NFRAMES - 1, &op) > 0) {
      if (pkt_copy(&c->frames[c->nframes], &op, 0) < 0) {
        th_encode_free(enc);
        return -1;
      }
      c->nframes++;
    }
  }
  th_encode_free(enc);
  th_comment_clear(&tc);
  return 0;
}

void corpus_free(corpus_t *c) {
  int i;
  for (i = 0; i < 3; i++) free(c->headers[i].data);
  for (i = 0; i < c->nframes; i++) free(c->frames[i].data);
  memset(c, 0, sizeof(*c));
}

th_dec_ctx *corpus_make_decoder(corpus_t *c) {
  th_info ti;
  th_comment tc;
  th_setup_info *setup = NULL;
  th_dec_ctx *dec = NULL;
  ogg_packet op;
  int i, ret = 0;

  th_info_init(&ti);
  th_comment_init(&tc);
  for (i = 0; i < 3; i++) {
    op.packet = c->headers[i].data;
    op.bytes = c->headers[i].len;
    op.b_o_s = (i == 0);
    op.e_o_s = 0;
    op.granulepos = 0;
    op.packetno = i;
    ret = th_decode_headerin(&ti, &tc, &setup, &op);
    if (ret < 0) break;
  }
  if (ret >= 0) dec = th_decode_alloc(&ti, setup);
  th_setup_free(setup);
  th_comment_clear(&tc);
  th_info_clear(&ti);
  return dec;
}
