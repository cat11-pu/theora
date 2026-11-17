/*Isolated benchmark for the three fragment reconstruction kernels only
  (no iDCT, no MV offset walk), so the clamp/LUT effect is measured directly.
  Usage: recon_bench <nfrags> [iters]*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ogg/ogg.h>
#include "state.h"

static long long now_ns(void){
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC,&ts);
  return (long long)ts.tv_sec*1000000000LL+ts.tv_nsec;
}

int main(int _argc,char **_argv){
  int            nblocks;
  int            iters;
  int            stride;
  int            i;
  int            j;
  int            k;
  unsigned       seed;
  unsigned char *dst;
  unsigned char *src1;
  unsigned char *src2;
  ogg_int16_t    residue[64];
  long long      t0;
  long long      t1;
  long long      intra_ns;
  long long      inter_ns;
  long long      inter2_ns;
  nblocks=_argc>1?atoi(_argv[1]):14400;
  iters=_argc>2?atoi(_argv[2]):2000;
  stride=16;
  dst=(unsigned char *)malloc((size_t)nblocks*8*stride+64);
  src1=(unsigned char *)malloc((size_t)nblocks*8*stride+64);
  src2=(unsigned char *)malloc((size_t)nblocks*8*stride+64);
  seed=42;
  for(i=0;i<nblocks*8*stride+64;i++){
    seed=seed*1103515245u+12345u;
    src1[i]=(unsigned char)(seed>>16);
    seed=seed*1103515245u+12345u;
    src2[i]=(unsigned char)(seed>>16);
  }
  for(k=0;k<64;k++)residue[k]=(ogg_int16_t)((k*37)%31-15);
  /*Warmup.*/
  for(i=0;i<nblocks;i++){
    unsigned char *d=dst+(size_t)i*8*stride;
    oc_frag_recon_intra_c(d,stride,residue);
  }
  intra_ns=inter_ns=inter2_ns=0;
  for(j=0;j<iters;j++){
    t0=now_ns();
    for(i=0;i<nblocks;i++){
      unsigned char *d=dst+(size_t)i*8*stride;
      oc_frag_recon_intra_c(d,stride,residue);
    }
    t1=now_ns();
    intra_ns+=t1-t0;
    t0=now_ns();
    for(i=0;i<nblocks;i++){
      unsigned char *d=dst+(size_t)i*8*stride;
      unsigned char *s=src1+(size_t)i*8*stride;
      oc_frag_recon_inter_c(d,s,stride,residue);
    }
    t1=now_ns();
    inter_ns+=t1-t0;
    t0=now_ns();
    for(i=0;i<nblocks;i++){
      unsigned char *d=dst+(size_t)i*8*stride;
      unsigned char *a=src1+(size_t)i*8*stride;
      unsigned char *b=src2+(size_t)i*8*stride;
      oc_frag_recon_inter2_c(d,a,b,stride,residue);
    }
    t1=now_ns();
    inter2_ns+=t1-t0;
  }
  printf("nblocks=%d iters=%d intra=%.2f inter=%.2f inter2=%.2f "
   "all_per_block=%.2f ns\n",
   nblocks,iters,
   (double)intra_ns/iters/nblocks,
   (double)inter_ns/iters/nblocks,
   (double)inter2_ns/iters/nblocks,
   (double)(intra_ns+inter_ns+inter2_ns)/iters/nblocks);
  if(dst[0]==254)printf("unlikely\n");
  free(dst);
  free(src1);
  free(src2);
  return 0;
}
