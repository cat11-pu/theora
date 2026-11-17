/*Decodes a stream N times in one process (fresh decoder each pass) and reports
  the minimum and median per-frame time. The minimum is the most robust against
  scheduler/load noise because it reflects the least-interrupted execution.
  Usage: dec_bench_min <input.ogv> <passes>*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <ogg/ogg.h>
#include <theora/theoradec.h>

static long long now_ns(void){
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC,&ts);
  return (long long)ts.tv_sec*1000000000LL+ts.tv_nsec;
}

static int cmp_ll(const void *_a,const void *_b){
  long long a=*(const long long *)_a;
  long long b=*(const long long *)_b;
  return (a>b)-(a<b);
}

int main(int _argc,char **_argv){
  int            passes;
  int            pi;
  long long     *avgs;
  if(_argc<3){fprintf(stderr,"usage: %s <ogv> <passes>\n",_argv[0]);return 1;}
  passes=atoi(_argv[2]);
  avgs=(long long *)malloc(sizeof(*avgs)*passes);
  for(pi=0;pi<passes;pi++){
    FILE             *fp;
    ogg_sync_state    oy;
    ogg_stream_state  os;
    ogg_page          page;
    ogg_packet        packet;
    th_info           ti;
    th_comment        tc;
    th_setup_info    *ts;
    th_dec_ctx       *tdc;
    th_ycbcr_buffer   ycbcr;
    int               got_stream;
    int               framei;
    long long         total;
    fp=fopen(_argv[1],"rb");
    ogg_sync_init(&oy);
    th_info_init(&ti);
    th_comment_init(&tc);
    ts=NULL;
    tdc=NULL;
    got_stream=0;
    framei=0;
    total=0;
    while(!feof(fp)){
      char *b=ogg_sync_buffer(&oy,4096);
      long  n=(long)fread(b,1,4096,fp);
      if(n<=0)break;
      ogg_sync_wrote(&oy,n);
      while(ogg_sync_pageout(&oy,&page)>0){
        if(!got_stream){
          ogg_stream_init(&os,ogg_page_serialno(&page));
          got_stream=1;
        }
        ogg_stream_pagein(&os,&page);
        while(ogg_stream_packetout(&os,&packet)>0){
          long long t0;
          long long t1;
          if(th_packet_isheader(&packet)){
            th_decode_headerin(&ti,&tc,&ts,&packet);
            continue;
          }
          if(tdc==NULL)tdc=th_decode_alloc(&ti,ts);
          t0=now_ns();
          if(th_decode_packetin(tdc,&packet,NULL)!=0)continue;
          if(th_decode_ycbcr_out(tdc,ycbcr)!=0)continue;
          t1=now_ns();
          total+=t1-t0;
          framei++;
        }
      }
    }
    avgs[pi]=total/(framei?framei:1);
    if(tdc)th_decode_free(tdc);
    th_comment_clear(&tc);
    th_info_clear(&ti);
    if(got_stream)ogg_stream_clear(&os);
    ogg_sync_clear(&oy);
    fclose(fp);
  }
  qsort(avgs,passes,sizeof(*avgs),cmp_ll);
  printf("min=%lld median=%lld max=%lld ns/frame over %d passes\n",
   avgs[0],avgs[passes/2],avgs[passes-1],passes);
  free(avgs);
  return 0;
}
