/*End-to-end Theora decode benchmark + per-frame pixel dumper.
  Usage: dec_bench <input.ogv> [dump_prefix]
  Prints per-frame decode time (ns); stderr gets the summary.
  Repeat timing by re-running the process (fresh decoder each time).*/
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

int main(int _argc,char **_argv){
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
  long long         t0;
  long long         t1;
  long long         total;
  const char       *dump_prefix;
  if(_argc<2){
    fprintf(stderr,"usage: %s <input.ogv> [dump_prefix]\n",_argv[0]);
    return 1;
  }
  dump_prefix=_argc>2?_argv[2]:NULL;
  fp=fopen(_argv[1],"rb");
  if(fp==NULL){perror("fopen");return 1;}
  ogg_sync_init(&oy);
  th_info_init(&ti);
  th_comment_init(&tc);
  ts=NULL;
  tdc=NULL;
  got_stream=0;
  framei=0;
  total=0;
  printf("# frame  decode_ns\n");
  while(!feof(fp)){
    char *buffer=ogg_sync_buffer(&oy,4096);
    long  bytes=(long)fread(buffer,1,4096,fp);
    if(bytes<=0)break;
    ogg_sync_wrote(&oy,bytes);
    while(ogg_sync_pageout(&oy,&page)>0){
      if(!got_stream){
        ogg_stream_init(&os,ogg_page_serialno(&page));
        got_stream=1;
      }
      ogg_stream_pagein(&os,&page);
      while(ogg_stream_packetout(&os,&packet)>0){
        if(th_packet_isheader(&packet)){
          if(th_decode_headerin(&ti,&tc,&ts,&packet)<0){
            fprintf(stderr,"bad header\n");return 1;
          }
          continue;
        }
        if(tdc==NULL){
          tdc=th_decode_alloc(&ti,ts);
          if(tdc==NULL){fprintf(stderr,"alloc failed\n");return 1;}
          th_setup_free(ts);
        }
        t0=now_ns();
        if(th_decode_packetin(tdc,&packet,NULL)!=0)continue;
        if(th_decode_ycbcr_out(tdc,ycbcr)!=0)continue;
        t1=now_ns();
        total+=t1-t0;
        printf("%d %lld\n",framei,(long long)(t1-t0));
        if(dump_prefix!=NULL){
          char  name[512];
          FILE *dfp;
          int   pli;
          int   yi;
          snprintf(name,sizeof(name),"%s_%04d.yuv",dump_prefix,framei);
          dfp=fopen(name,"wb");
          if(dfp==NULL){perror("dump");return 1;}
          for(pli=0;pli<3;pli++){
            int width=ycbcr[pli].width;
            int height=ycbcr[pli].height;
            for(yi=0;yi<height;yi++){
              fwrite(ycbcr[pli].data+yi*ycbcr[pli].stride,1,width,dfp);
            }
          }
          fclose(dfp);
        }
        framei++;
      }
    }
  }
  fprintf(stderr,"frames=%d total_ns=%lld avg_ns=%lld\n",
   framei,total,framei?total/framei:0);
  if(tdc!=NULL)th_decode_free(tdc);
  th_comment_clear(&tc);
  th_info_clear(&ti);
  if(got_stream)ogg_stream_clear(&os);
  ogg_sync_clear(&oy);
  fclose(fp);
  return 0;
}
