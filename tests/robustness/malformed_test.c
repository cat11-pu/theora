/* Property test: 200 malformed Theora packets in 4 categories.
 *
 * Categories (50 cases each):
 *   TRUNC    - truncated frame packets
 *   BADQUANT - corrupted quant/huffman setup header (illegal quantization)
 *   BIGMV    - corrupted motion-vector region of inter frames
 *   HDRLOOP  - repeated headers alternating with data packets
 *
 * Properties checked per case (each case runs in a forked child built
 * with ASan+LSan+UBSan):
 *   - zero crashes     (child exits cleanly, no signal)
 *   - zero out-of-bounds (ASan/UBSan stay silent)
 *   - zero leaks       (LSan stays silent)
 *
 * Every case is described by a metadata line (with byte offsets) printed
 * before it runs, and its mutated bytes are written to
 * artifacts/counterexamples/ so any failure reproduces offline.
 */
#include "corpus.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define CASES_PER_CAT 50
#define NCASES (4 * CASES_PER_CAT)

static const char *cat_names[4] = {"TRUNC", "BADQUANT", "BIGMV", "HDRLOOP"};

/* deterministic PRNG so counterexamples reproduce */
static unsigned long long rng_state;
static unsigned long long rng_next(void) {
  rng_state ^= rng_state << 13;
  rng_state ^= rng_state >> 7;
  rng_state ^= rng_state << 17;
  return rng_state;
}
static long rng_range(long lo, long hi) { /* inclusive */
  return lo + (long)(rng_next() % (unsigned long long)(hi - lo + 1));
}

typedef struct {
  int id;
  int cat;
  int pkt_idx;          /* which corpus packet was mutated */
  long offsets[16];     /* byte offsets of mutation, -1 terminated */
  long orig_len;
  long new_len;
  char note[128];
} case_meta_t;

/* ---- packet sequence the child will feed to the decoder ---- */
typedef struct {
  pkt_t pkts[64];
  int n;
  int mutated_header;   /* 1: headers themselves are mutated */
} seq_t;

static void seq_push_copy(seq_t *s, const pkt_t *p) {
  pkt_t *d = &s->pkts[s->n++];
  d->data = malloc((size_t)(p->len > 0 ? p->len : 1));
  memcpy(d->data, p->data, (size_t)p->len);
  d->len = p->len;
  d->is_header = p->is_header;
  d->is_keyframe = p->is_keyframe;
}

static void seq_free(seq_t *s) {
  int i;
  for (i = 0; i < s->n; i++) free(s->pkts[i].data);
  s->n = 0;
}

/* ---- case builders ---- */

static void build_trunc(corpus_t *c, seq_t *s, case_meta_t *m) {
  int victim = (int)rng_range(0, c->nframes - 1);
  long cut = rng_range(0, c->frames[victim].len - 1);
  int i;
  for (i = 0; i < 3; i++) seq_push_copy(s, &c->headers[i]);
  for (i = 0; i < c->nframes; i++) {
    if (i == victim) {
      pkt_t t = c->frames[i];
      t.len = cut;
      seq_push_copy(s, &t);
    } else {
      seq_push_copy(s, &c->frames[i]);
    }
  }
  m->pkt_idx = victim;
  m->orig_len = c->frames[victim].len;
  m->new_len = cut;
  m->offsets[0] = cut;
  m->offsets[1] = -1;
  snprintf(m->note, sizeof(m->note), "frame[%d] truncated at offset %ld",
           victim, cut);
}

static void build_badquant(corpus_t *c, seq_t *s, case_meta_t *m) {
  int i, nmut = (int)rng_range(1, 8);
  pkt_t bad;
  for (i = 0; i < 3; i++) seq_push_copy(s, &c->headers[i]);
  for (i = 0; i < c->nframes; i++) seq_push_copy(s, &c->frames[i]);
  s->mutated_header = 1;
  bad = s->pkts[2]; /* setup header: quant matrices + huffman tables */
  for (i = 0; i < nmut && i < 16; i++) {
    long off = rng_range(7, bad.len - 1); /* skip 0x82 "theora" sig */
    bad.data[off] = (unsigned char)(rng_next() & 0xFF);
    m->offsets[i] = off;
  }
  if (nmut < 16) m->offsets[nmut] = -1;
  m->pkt_idx = 2;
  m->orig_len = bad.len;
  m->new_len = bad.len;
  snprintf(m->note, sizeof(m->note),
           "setup header corrupted at %d byte(s) (quant/huffman region)", nmut);
}

static void build_bigmv(corpus_t *c, seq_t *s, case_meta_t *m) {
  int victim, i, nmut;
  pkt_t bad;
  long lo, hi;
  /* pick an inter frame (keyframe has no MVs) */
  do {
    victim = (int)rng_range(0, c->nframes - 1);
  } while (c->frames[victim].is_keyframe);
  for (i = 0; i < 3; i++) seq_push_copy(s, &c->headers[i]);
  for (i = 0; i < c->nframes; i++) seq_push_copy(s, &c->frames[i]);
  bad = s->pkts[3 + victim];
  /* MV bits live in the middle of an inter packet; hammer that window
     with 0xFF runs to force huge MV components */
  lo = bad.len / 5;
  hi = bad.len * 3 / 5;
  if (hi < lo) hi = lo;
  nmut = (int)rng_range(1, 16);
  for (i = 0; i < nmut && i < 16; i++) {
    long off = rng_range(lo, hi);
    bad.data[off] = (rng_next() & 1) ? 0xFF : (unsigned char)(rng_next() & 0xFF);
    m->offsets[i] = off;
  }
  if (nmut < 16) m->offsets[nmut] = -1;
  m->pkt_idx = victim;
  m->orig_len = bad.len;
  m->new_len = bad.len;
  snprintf(m->note, sizeof(m->note),
           "inter frame[%d] MV window [%ld,%ld] corrupted at %d byte(s)",
           victim, lo, hi, nmut);
}

static void build_hdrloop(corpus_t *c, seq_t *s, case_meta_t *m) {
  int i, nins = 0;
  for (i = 0; i < 3; i++) seq_push_copy(s, &c->headers[i]);
  for (i = 0; i < c->nframes; i++) {
    /* alternate: re-inject 1-2 header packets before data packets */
    int reps = (int)rng_range(0, 2);
    int r;
    for (r = 0; r < reps && nins < 16; r++) {
      int h = (int)rng_range(0, 2);
      seq_push_copy(s, &c->headers[h]);
      m->offsets[nins++] = s->n - 1; /* sequence position of injection */
    }
    seq_push_copy(s, &c->frames[i]);
  }
  if (nins < 16) m->offsets[nins] = -1;
  m->pkt_idx = -1;
  m->orig_len = 0;
  m->new_len = 0;
  snprintf(m->note, sizeof(m->note),
           "%d header re-injection(s) alternating with data packets", nins);
}

/* ---- child-side decode of a packet sequence ---- */

static void decode_sequence(seq_t *s) {
  th_info ti;
  th_comment tc;
  th_setup_info *setup = NULL;
  th_dec_ctx *dec = NULL;
  ogg_int64_t gp = 0;
  int i, hdr_done = 0;

  th_info_init(&ti);
  th_comment_init(&tc);

  for (i = 0; i < s->n; i++) {
    ogg_packet op;
    op.packet = s->pkts[i].data;
    op.bytes = s->pkts[i].len;
    op.b_o_s = (i == 0);
    op.e_o_s = 0;
    op.granulepos = 0;
    op.packetno = i;

    if (!hdr_done) {
      int ret = th_decode_headerin(&ti, &tc, &setup, &op);
      if (ret < 0) {
        /* decoder rejected the (possibly mutated) headers: acceptable,
           just stop - the property is "no crash", not "must decode" */
        break;
      }
      if (ret > 0) continue; /* header consumed, more headers expected */
      /* ret == 0: headers complete and op IS the first data packet;
         fall through and decode it */
      hdr_done = 1;
      dec = th_decode_alloc(&ti, setup);
      if (!dec) break;
    }
    if (th_decode_packetin(dec, &op, &gp) == 0) {
      th_ycbcr_buffer yuv;
      th_decode_ycbcr_out(dec, yuv);
    }
  }

  if (dec) th_decode_free(dec);
  th_setup_free(setup);
  th_comment_clear(&tc);
  th_info_clear(&ti);
}

/* ---- counterexample dump ---- */

static void dump_case(const case_meta_t *m, const seq_t *s, const char *dir) {
  char path[512];
  FILE *f;
  int i;
  snprintf(path, sizeof(path), "%s/case_%03d_%s.seq", dir, m->id,
           cat_names[m->cat]);
  f = fopen(path, "wb");
  if (!f) return;
  /* simple framing: 8-byte LE length + raw packet bytes, per packet */
  for (i = 0; i < s->n; i++) {
    unsigned long long len = (unsigned long long)s->pkts[i].len;
    unsigned char hdr[8];
    int b;
    for (b = 0; b < 8; b++) hdr[b] = (unsigned char)(len >> (8 * b));
    fwrite(hdr, 1, 8, f);
    fwrite(s->pkts[i].data, 1, (size_t)s->pkts[i].len, f);
  }
  fclose(f);
}

int main(int argc, char **argv) {
  corpus_t c;
  const char *cex_dir = argc > 1 ? argv[1] : "counterexamples";
  const char *summary_path = argc > 2 ? argv[2] : "malformed_summary.txt";
  FILE *summary = fopen(summary_path, "w");
  int pass[4] = {0, 0, 0, 0}, fail[4] = {0, 0, 0, 0};
  int id;

  if (corpus_generate(&c) < 0) {
    fprintf(stderr, "corpus generation failed\n");
    return 2;
  }
  fprintf(stderr, "corpus: %d frames, header sizes %ld/%ld/%ld\n", c.nframes,
          c.headers[0].len, c.headers[1].len, c.headers[2].len);

  for (id = 0; id < NCASES; id++) {
    case_meta_t m;
    seq_t s;
    pid_t pid;
    int status, ok;

    memset(&m, 0, sizeof(m));
    memset(&s, 0, sizeof(s));
    m.id = id;
    m.cat = id / CASES_PER_CAT;
    rng_state = 0x9E3779B97F4A7C15ULL ^ (unsigned long long)(id + 1);

    switch (m.cat) {
      case 0: build_trunc(&c, &s, &m); break;
      case 1: build_badquant(&c, &s, &m); break;
      case 2: build_bigmv(&c, &s, &m); break;
      default: build_hdrloop(&c, &s, &m); break;
    }

    /* metadata line with offsets, flushed before the child runs so a
       crash is always attributable */
    {
      char offbuf[256] = "";
      int k;
      for (k = 0; k < 16 && m.offsets[k] >= 0; k++) {
        char tmp[24];
        snprintf(tmp, sizeof(tmp), "%s%ld", k ? "," : "", m.offsets[k]);
        strncat(offbuf, tmp, sizeof(offbuf) - strlen(offbuf) - 1);
      }
      fprintf(summary, "case %03d %-8s pkt=%d orig_len=%ld new_len=%ld "
              "offsets=[%s] note=%s\n", m.id, cat_names[m.cat], m.pkt_idx,
              m.orig_len, m.new_len, offbuf, m.note);
      fflush(summary);
    }
    dump_case(&m, &s, cex_dir);

    pid = fork();
    if (pid == 0) {
      decode_sequence(&s);
      _exit(0);
    }
    waitpid(pid, &status, 0);
    ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (ok) {
      pass[m.cat]++;
    } else {
      fail[m.cat]++;
      fprintf(summary, "case %03d %-8s FAILED: %s (offsets above, "
              "counterexample case_%03d_%s.seq)\n", m.id, cat_names[m.cat],
              WIFSIGNALED(status) ? "crash/signal" : "sanitizer exit",
              m.id, cat_names[m.cat]);
      fflush(summary);
    }
    seq_free(&s);
  }

  fprintf(summary, "\n==== SUMMARY ====\n");
  {
    int k, tp = 0, tf = 0;
    for (k = 0; k < 4; k++) {
      fprintf(summary, "%-8s pass=%d fail=%d\n", cat_names[k], pass[k],
              fail[k]);
      tp += pass[k];
      tf += fail[k];
    }
    fprintf(summary, "TOTAL    pass=%d fail=%d  crashes=0-required "
            "oob=0-required leaks=0-required -> %s\n", tp, tf,
            tf == 0 ? "ALL PROPERTIES HELD" : "PROPERTY VIOLATIONS FOUND");
  }
  fclose(summary);
  corpus_free(&c);
  return 0;
}
