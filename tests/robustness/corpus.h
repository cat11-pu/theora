/* Shared corpus generation for decoder robustness tests.
   Encodes a small valid Theora stream in memory so the malformed-frame
   tests start from decodable packets. */
#ifndef CORPUS_HDR_INCLUDED
#define CORPUS_HDR_INCLUDED

#include <theora/theoraenc.h>
#include <theora/theoradec.h>

#define CORPUS_W 160
#define CORPUS_H 96
#define CORPUS_NFRAMES 8

typedef struct {
  unsigned char *data;
  long len;
  int is_header;
  int is_keyframe;
} pkt_t;

typedef struct {
  pkt_t headers[3];
  pkt_t frames[CORPUS_NFRAMES];
  int nframes;
  th_info info;
} corpus_t;

void corpus_fill_frame(th_ycbcr_buffer yuv, int frame_no);
int corpus_generate(corpus_t *c);
void corpus_free(corpus_t *c);
th_dec_ctx *corpus_make_decoder(corpus_t *c);

#endif
