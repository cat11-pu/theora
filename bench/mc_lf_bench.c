/*Micro-benchmark for the motion-compensation reconstruction kernels and the
  in-loop deblocking filter, on a synthetic oc_theora_state.
  Usage: mc_lf_bench <width> <height> [iters]
  Links the real libtheora C internals so it tracks production code.*/
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

static unsigned char rnd_byte(unsigned *_seed){
  *_seed=*_seed*1103515245u+12345u;
  return (unsigned char)(*_seed>>16);
}

int main(int _argc,char **_argv){
  int            width;
  int            height;
  int            iters;
  int            nhfrags;
  int            nvfrags;
  int            nfrags;
  int            stride;
  int            frame_bytes;
  int            pli;
  int            fi;
  unsigned       seed;
  oc_theora_state st;
  unsigned char *frame;
  unsigned char *ref;
  ogg_int16_t    coeffs[128];
  signed char    bv[256];
  long long      t0;
  long long      t1;
  long long      mc_ns;
  long long      lf_ns;
  long long      iters_long;
  if(_argc<3){
    fprintf(stderr,"usage: %s <width> <height> [iters]\n",_argv[0]);
    return 1;
  }
  width=atoi(_argv[1]);
  height=atoi(_argv[2]);
  iters=_argc>3?atoi(_argv[3]):200;
  nhfrags=(width+7)/8;
  nvfrags=(height+7)/8;
  nfrags=nhfrags*nvfrags;
  /*Stride with padding so fractional MVs near edges stay in bounds (the real
     decoder keeps border pixels for the same reason).*/
  stride=nhfrags*8+32;
  frame_bytes=(nvfrags*8+32)*stride;
  memset(&st,0,sizeof(st));
  oc_state_accel_init_c(&st);
  st.info.pixel_fmt=TH_PF_420;
  st.ref_ystride[0]=stride;
  st.ref_ystride[1]=st.ref_ystride[2]=stride/2;
  frame=(unsigned char *)malloc((size_t)frame_bytes);
  ref=(unsigned char *)malloc((size_t)frame_bytes);
  st.frags=(oc_fragment *)calloc((size_t)nfrags,sizeof(*st.frags));
  st.frag_buf_offs=(ptrdiff_t *)malloc(sizeof(*st.frag_buf_offs)*nfrags);
  st.frag_mvs=(oc_mv *)malloc(sizeof(*st.frag_mvs)*nfrags);
  seed=1234;
  for(fi=0;fi<frame_bytes;fi++)ref[fi]=rnd_byte(&seed);
  memcpy(frame,ref,(size_t)frame_bytes);
  /*Luma plane layout with a 16px border.*/
  st.fplanes[0].nhfrags=nhfrags;
  st.fplanes[0].nvfrags=nvfrags;
  st.fplanes[0].froffset=0;
  st.fplanes[0].nfrags=nfrags;
  st.ref_frame_data[OC_FRAME_SELF]=frame;
  st.ref_frame_data[OC_FRAME_PREV]=ref;
  for(fi=0;fi<nfrags;fi++){
    int fx=fi%nhfrags;
    int fy=fi/nhfrags;
    st.frag_buf_offs[fi]=(ptrdiff_t)(16+fy*8)*stride+(16+fx*8);
    st.frags[fi].coded=1;
    /*Two thirds of blocks use a reference, half of those fractional (inter2).*/
    st.frags[fi].refi=(fi%3==0)?OC_FRAME_SELF:OC_FRAME_PREV;
    /*MVs spanning -16..15 full pixels with mixed fractional phases.*/
    {
      int mxx=((fi*7)%65)-32;
      int myy=((fi*13)%65)-32;
      st.frag_mvs[fi]=OC_MV((signed char)mxx,(signed char)myy);
    }
  }
  for(pli=0;pli<128;pli++)coeffs[pli]=(ogg_int16_t)((pli*37)%31-15);
  /*A representative filter limit (ffmpeg/theora streams commonly use 8..24).*/
  st.qis[0]=16;
  st.loop_filter_limits[16]=16;
  oc_loop_filter_init_c(bv,16);
  /*Warmup.*/
  for(fi=0;fi<nfrags;fi++){
    oc_state_frag_recon_c(&st,fi,0,coeffs,3,32);
  }
  mc_ns=0;
  for(iters_long=0;iters_long<iters;iters_long++){
    t0=now_ns();
    for(fi=0;fi<nfrags;fi++){
      oc_state_frag_recon_c(&st,fi,0,coeffs,3,32);
    }
    t1=now_ns();
    mc_ns+=t1-t0;
  }
  lf_ns=0;
  for(iters_long=0;iters_long<iters;iters_long++){
    t0=now_ns();
    oc_state_loop_filter_frag_rows_c(&st,bv,OC_FRAME_SELF,0,0,nvfrags);
    t1=now_ns();
    lf_ns+=t1-t0;
  }
  printf("%dx%d nfrags=%d iters=%d "
   "mc_total_ns=%lld mc_per_block_ns=%.2f "
   "lf_total_ns=%lld lf_per_block_ns=%.2f mc+lf_per_frame_ns=%lld\n",
   width,height,nfrags,iters,
   mc_ns,(double)mc_ns/iters/nfrags,
   lf_ns,(double)lf_ns/iters/nfrags,
   (mc_ns+lf_ns)/iters);
  /*Touch output so the compiler cannot elide the loops.*/
  if(frame[0]==255)printf("unlikely\n");
  free(frame);
  free(ref);
  free(st.frags);
  free(st.frag_buf_offs);
  free(st.frag_mvs);
  return 0;
}
