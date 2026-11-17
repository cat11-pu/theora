/*Isolated loop-filter benchmark over a synthetic frame buffer.
  Usage: lf_bench <width> <height> [iters]
  Calls the real oc_state_loop_filter_frag_rows_c via a minimal state.*/
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

static unsigned char rndb(unsigned *_s){
  *_s=*_s*1103515245u+12345u;
  return (unsigned char)(*_s>>16);
}

int main(int _argc,char **_argv){
  int               width;
  int               height;
  int               iters;
  int               nhfrags;
  int               nvfrags;
  int               nfrags;
  int               stride;
  int               nbytes;
  int               fi;
  unsigned          seed;
  oc_theora_state   st;
  unsigned char    *frame;
  signed char       bv[256];
  long long         t0;
  long long         t1;
  long long         acc;
  if(_argc<3){fprintf(stderr,"usage: %s w h [iters]\n",_argv[0]);return 1;}
  width=atoi(_argv[1]);
  height=atoi(_argv[2]);
  iters=_argc>3?atoi(_argv[3]):2000;
  nhfrags=(width+7)/8;
  nvfrags=(height+7)/8;
  nfrags=nhfrags*nvfrags;
  stride=nhfrags*8+32;
  nbytes=(nvfrags*8+32)*stride;
  memset(&st,0,sizeof(st));
  st.info.pixel_fmt=TH_PF_420;
  st.ref_ystride[0]=stride;
  st.fplanes[0].nhfrags=nhfrags;
  st.fplanes[0].nvfrags=nvfrags;
  st.fplanes[0].froffset=0;
  st.fplanes[0].nfrags=nfrags;
  frame=(unsigned char *)malloc((size_t)nbytes);
  st.frags=(oc_fragment *)calloc((size_t)nfrags,sizeof(*st.frags));
  st.frag_buf_offs=(ptrdiff_t *)malloc(sizeof(*st.frag_buf_offs)*nfrags);
  st.ref_frame_data[OC_FRAME_SELF]=frame;
  seed=777;
  for(fi=0;fi<nbytes;fi++)frame[fi]=rndb(&seed);
  for(fi=0;fi<nfrags;fi++){
    int fx=fi%nhfrags;
    int fy=fi/nhfrags;
    st.frag_buf_offs[fi]=(ptrdiff_t)(16+fy*8)*stride+(16+fx*8);
    /*Mix of coded/uncoded fragments to exercise all four edge cases.*/
    st.frags[fi].coded=(fi%4!=0);
  }
  oc_loop_filter_init_c(bv,16);
  oc_state_loop_filter_frag_rows_c(&st,bv,OC_FRAME_SELF,0,0,nvfrags);
  acc=0;
  for(fi=0;fi<iters;fi++){
    t0=now_ns();
    oc_state_loop_filter_frag_rows_c(&st,bv,OC_FRAME_SELF,0,0,nvfrags);
    t1=now_ns();
    acc+=t1-t0;
  }
  printf("%dx%d nfrags=%d iters=%d lf_per_block_ns=%.2f lf_per_frame_ns=%lld\n",
   width,height,nfrags,iters,(double)acc/iters/nfrags,acc/iters);
  if(frame[0]==254)printf("unlikely\n");
  free(frame);
  free(st.frags);
  free(st.frag_buf_offs);
  return 0;
}
