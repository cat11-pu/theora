#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <ogg/ogg.h>
#include <theora/theoraenc.h>
#include <theora/theoradec.h>

/*Frame geometry: must be a multiple of 16 for 4:2:0.*/
#define FRAME_W        64
#define FRAME_H        48
/*One keyframe every 1 << KEYFRAME_SHIFT frames, i.e. every 16 frames.*/
#define KEYFRAME_SHIFT 4
#define KEYFRAME_GAP   (1 << KEYFRAME_SHIFT)
/*Number of consecutive frames that are dropped before the next keyframe.*/
#define DROP_COUNT     10
/*Total number of frames produced by the encoder.*/
#define FRAME_COUNT    (KEYFRAME_GAP * 3)

#define FAIL(str) \
  do{ printf("%s:%d: %s\n", __FILE__, __LINE__, (str)); exit(1); }while(0)
#define INFO(str) printf("----  %s ...\n",(str))

/*Deep-copy an ogg_packet: the encoder reuses its internal buffer once the
   next packet is produced, so every packet must own its payload.*/
static ogg_packet clone_packet(const ogg_packet *_op){
  ogg_packet copy;
  copy=*_op;
  if(_op->bytes>0){
    copy.packet=(unsigned char *)malloc((size_t)_op->bytes);
    if(copy.packet==NULL)FAIL("out of memory");
    memcpy(copy.packet,_op->packet,(size_t)_op->bytes);
  }
  else copy.packet=NULL;
  return copy;
}

/*A Theora data packet starts with a 0 bit; the next bit is the frame type,
   0 for an intra/key frame and 1 for an inter frame.*/
static int packet_is_keyframe(const ogg_packet *_op){
  return _op->bytes>0&&(_op->packet[0]&0x40)==0;
}

/*A raw, packet-level view of the encoded stream: the three header packets
   followed by one packet per coded frame.*/
typedef struct{
  ogg_packet *packets;
  int         npackets;
  int         nheaders;
  int        *keyframe;
} test_stream;

static void fill_frame(th_ycbcr_buffer buf,int frame){
  int pli;
  int y;
  int x;
  for(pli=0;pli<3;pli++){
    unsigned char *row=buf[pli].data;
    for(y=0;y<buf[pli].height;y++){
      for(x=0;x<buf[pli].width;x++){
        row[x]=(unsigned char)(frame*7+y*3+x*5+(pli==0?0:128));
      }
      row+=buf[pli].stride;
    }
  }
}

/*Encode FRAME_COUNT frames in memory and keep every packet.*/
static test_stream make_stream(void){
  th_enc_ctx    *enc;
  th_info        info;
  th_comment     comment;
  th_ycbcr_buffer buf;
  unsigned char *plane[3];
  ogg_packet     op;
  test_stream    st;
  int            ret;
  int            frame;
  int            capacity;

  th_info_init(&info);
  info.frame_width=FRAME_W;
  info.frame_height=FRAME_H;
  info.pic_width=FRAME_W;
  info.pic_height=FRAME_H;
  info.pic_x=0;
  info.pic_y=0;
  info.colorspace=TH_CS_UNSPECIFIED;
  info.pixel_fmt=TH_PF_420;
  info.target_bitrate=0;
  info.quality=48;
  info.fps_numerator=1;
  info.fps_denominator=1;
  info.aspect_numerator=1;
  info.aspect_denominator=1;
  info.keyframe_granule_shift=KEYFRAME_SHIFT;

  enc=th_encode_alloc(&info);
  if(enc==NULL)FAIL("th_encode_alloc failed");
  th_info_clear(&info);

  capacity=8+FRAME_COUNT;
  st.packets=(ogg_packet *)calloc((size_t)capacity,sizeof(*st.packets));
  st.keyframe=(int *)calloc((size_t)capacity,sizeof(*st.keyframe));
  if(st.packets==NULL||st.keyframe==NULL)FAIL("out of memory");
  st.npackets=0;

  th_comment_init(&comment);
  while(th_encode_flushheader(enc,&comment,&op)>0){
    st.packets[st.npackets]=clone_packet(&op);
    st.npackets++;
  }
  st.nheaders=st.npackets;
  th_comment_clear(&comment);

  /*Set up a tightly packed 4:2:0 source buffer.*/
  buf[0].width=FRAME_W;
  buf[0].height=FRAME_H;
  buf[0].stride=FRAME_W;
  buf[1].width=buf[2].width=FRAME_W/2;
  buf[1].height=buf[2].height=FRAME_H/2;
  buf[1].stride=buf[2].stride=FRAME_W/2;
  plane[0]=(unsigned char *)malloc((size_t)FRAME_W*FRAME_H);
  plane[1]=(unsigned char *)malloc((size_t)(FRAME_W/2)*(FRAME_H/2));
  plane[2]=(unsigned char *)malloc((size_t)(FRAME_W/2)*(FRAME_H/2));
  if(plane[0]==NULL||plane[1]==NULL||plane[2]==NULL)FAIL("out of memory");
  buf[0].data=plane[0];
  buf[1].data=plane[1];
  buf[2].data=plane[2];
  for(frame=0;frame<FRAME_COUNT;frame++){
    fill_frame(buf,frame);
    ret=th_encode_ycbcr_in(enc,buf);
    if(ret<0)FAIL("th_encode_ycbcr_in failed");
    while(th_encode_packetout(enc,frame==FRAME_COUNT-1,&op)>0){
      if(st.npackets>=capacity)FAIL("packet table overflow");
      st.packets[st.npackets]=clone_packet(&op);
      st.keyframe[st.npackets]=packet_is_keyframe(&op);
      st.npackets++;
    }
  }
  th_encode_free(enc);
  free(plane[0]);
  free(plane[1]);
  free(plane[2]);
  if(st.npackets!=st.nheaders+FRAME_COUNT)FAIL("unexpected packet count");
  return st;
}

static void stream_free(test_stream *st){
  int pi;
  for(pi=0;pi<st->npackets;pi++)free(st->packets[pi].packet);
  free(st->packets);
  free(st->keyframe);
}

static th_dec_ctx *open_decoder(const test_stream *st){
  th_info     info;
  th_comment  comment;
  th_setup_info *setup;
  th_dec_ctx *dec;
  int         ret;
  int         pi;
  th_info_init(&info);
  th_comment_init(&comment);
  setup=NULL;
  for(pi=0;pi<st->nheaders;pi++){
    ret=th_decode_headerin(&info,&comment,&setup,&st->packets[pi]);
    if(ret<0)FAIL("th_decode_headerin failed");
  }
  th_comment_clear(&comment);
  dec=th_decode_alloc(&info,setup);
  if(dec==NULL)FAIL("th_decode_alloc failed");
  th_setup_free(setup);
  th_info_clear(&info);
  return dec;
}

/*A 0-byte packet is the standard in-band signal for a dropped frame: the
   reference frame it would have produced is missing.*/
static ogg_packet make_gap_packet(void){
  ogg_packet op;
  memset(&op,0,sizeof(op));
  op.packet=NULL;
  op.bytes=0;
  op.b_o_s=0;
  op.e_o_s=0;
  op.granulepos=-1;
  op.packetno=0;
  return op;
}

static void assert_decoded_frame(th_dec_ctx *dec){
  th_ycbcr_buffer out;
  int              ret;
  ret=th_decode_ycbcr_out(dec,out);
  if(ret<0)FAIL("th_decode_ycbcr_out failed after recovery");
  if(out[0].width!=FRAME_W||out[0].height!=FRAME_H)
   FAIL("recovered frame has wrong geometry");
}

/*Scenario 1: decode starts on a keyframe, then DROP_COUNT consecutive frames
   are lost.  Every lost frame and every inter frame that depends on it must
   be rejected with an error instead of producing a gray/garbled picture or
   reading out of bounds.  The next keyframe must resync the decoder.*/
static void test_gap_recovery(const test_stream *st){
  th_dec_ctx *dec;
  ogg_packet  gap;
  int         errors;
  int         recovered;
  int         pi;
  int         frame;
  /*Frame index at which the burst of packet loss begins and ends.*/
  int         gap_start;
  int         gap_end;
  INFO("scenario 1: 10-frame gap followed by a keyframe");
  dec=open_decoder(st);
  gap=make_gap_packet();
  errors=0;
  recovered=0;
  gap_start=1;
  gap_end=gap_start+DROP_COUNT;
  frame=0;
  for(pi=st->nheaders;pi<st->npackets;pi++,frame++){
    ogg_int64_t granpos;
    int         ret;
    if(frame>=gap_start&&frame<gap_end){
      ret=th_decode_packetin(dec,&gap,&granpos);
      if(ret>=0)FAIL("missing reference frame was not rejected");
      if(ret!=TH_EBADPACKET)FAIL("unexpected error code for dropped frame");
      errors++;
      continue;
    }
    ret=th_decode_packetin(dec,&st->packets[pi],&granpos);
    if(frame<gap_start){
      /*Frames before the loss decode normally.*/
      if(ret<0)FAIL("frame before the gap failed to decode");
      if(ret==0)assert_decoded_frame(dec);
    }
    else if(!recovered&&!st->keyframe[pi]){
      /*Inter frames between the loss and the next keyframe must be skipped.*/
      if(ret!=TH_EBADPACKET)
       FAIL("dependent inter frame was not rejected");
      errors++;
    }
    else if(!recovered&&st->keyframe[pi]){
      /*The first keyframe after the loss resynchronizes the decoder.*/
      if(ret<0)FAIL("keyframe after the gap failed to decode");
      recovered=1;
      assert_decoded_frame(dec);
    }
    else{
      /*Once recovered, every later frame decodes without errors.*/
      if(ret<0)FAIL("decoder did not stay healthy after recovery");
      if(ret==0)assert_decoded_frame(dec);
    }
  }
  if(errors==0)FAIL("no missing-reference errors were reported");
  if(!recovered)FAIL("decoder never recovered on the next keyframe");
  th_decode_free(dec);
  printf("----  %d frames rejected, recovery on keyframe OK\n",errors);
}

/*Scenario 2: the decoder is started on an inter frame with no reference
   frame at all (e.g. the leading keyframe was lost before playback).
  This must fail cleanly rather than touching uninitialized references.*/
static void test_start_on_inter(const test_stream *st){
  th_dec_ctx *dec;
  ogg_packet  gap;
  int         pi;
  int         ret;
  int         recovered;
  INFO("scenario 2: inter frame before any keyframe");
  dec=open_decoder(st);
  gap=make_gap_packet();
  recovered=0;
  for(pi=st->nheaders;pi<st->npackets;pi++){
    ogg_int64_t granpos;
    if(st->keyframe[pi]){
      ret=th_decode_packetin(dec,&st->packets[pi],&granpos);
      if(ret<0)FAIL("keyframe failed when resyncing from a cold start");
      recovered=1;
      assert_decoded_frame(dec);
      break;
    }
    if(pi==st->nheaders){
      /*Feed the real inter packet once: it has no reference to use.*/
      ret=th_decode_packetin(dec,&st->packets[pi],&granpos);
      if(ret!=TH_EBADPACKET)
       FAIL("inter frame without any reference was not rejected");
    }
    else{
      ret=th_decode_packetin(dec,&gap,&granpos);
      if(ret!=TH_EBADPACKET)FAIL("gap packet was not rejected");
    }
  }
  if(!recovered)FAIL("no keyframe was found for cold-start recovery");
  th_decode_free(dec);
  printf("----  cold-start rejection and keyframe recovery OK\n");
}

int main(void){
  test_stream st;
  st=make_stream();
  printf("----  encoded %d frames, %d header packets\n",
   st.npackets-st.nheaders,st.nheaders);
  test_gap_recovery(&st);
  test_start_on_inter(&st);
  stream_free(&st);
  printf("ALL REFERENCE-LOSS TESTS PASSED\n");
  return 0;
}
