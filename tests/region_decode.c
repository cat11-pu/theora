/*skeleton test for rectangular, row-oriented decoder output.*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <theora/theoraenc.h>
#include <theora/theoradec.h>

#include "tests.h"

#define FRAME_WIDTH  (64)
#define FRAME_HEIGHT (64)
#define FRAME_COUNT  (8)
#define GUARD_BYTES  (32)

typedef struct{
  unsigned char *data;
  long           bytes;
  ogg_int64_t    granulepos;
  long           packetno;
  int            b_o_s;
  int            e_o_s;
}packet_copy;

typedef struct{
  packet_copy *packets;
  int          npackets;
  int          capacity;
}packet_list;

typedef struct{
  int x;
  int y;
  int width;
  int height;
}region_spec;

static const region_spec TEST_REGIONS[]={
  { 0, 0,64,64},
  { 0, 0, 1, 1},
  {63, 0, 1, 1},
  { 0,63, 1, 1},
  {63,63, 1, 1},
  { 2, 4,58,52},
  { 3, 5, 7, 5},
  {17, 9,13,47},
  {31, 0, 1,64},
  { 0,31,64, 1},
  {61,59, 3, 5}
};

static int g_checks;
static size_t g_bytes;
static unsigned long long g_hash=1469598103934665603ULL;

static void hash_bytes(const unsigned char *_data,size_t _size){
  size_t i;
  g_bytes+=_size;
  for(i=0;i<_size;i++){
    g_hash^=_data[i];
    g_hash*=1099511628211ULL;
  }
}

static void packet_list_init(packet_list *_list){
  memset(_list,0,sizeof(*_list));
}

static void packet_list_append(packet_list *_list,const ogg_packet *_op){
  packet_copy *packet;
  if(_list->npackets>=_list->capacity){
    int          capacity;
    packet_copy *packets;
    capacity=_list->capacity*2+8;
    packets=(packet_copy *)realloc(_list->packets,
     capacity*sizeof(*packets));
    if(packets==NULL)FAIL("out of memory growing packet list");
    _list->packets=packets;
    _list->capacity=capacity;
  }
  packet=_list->packets+_list->npackets++;
  packet->bytes=_op->bytes;
  packet->data=(unsigned char *)malloc((size_t)packet->bytes);
  if(packet->data==NULL&&packet->bytes>0)FAIL("out of memory copying packet");
  if(packet->bytes>0)memcpy(packet->data,_op->packet,(size_t)packet->bytes);
  packet->granulepos=_op->granulepos;
  packet->packetno=_op->packetno;
  packet->b_o_s=_op->b_o_s;
  packet->e_o_s=_op->e_o_s;
}

static void packet_list_clear(packet_list *_list){
  int i;
  for(i=0;i<_list->npackets;i++)free(_list->packets[i].data);
  free(_list->packets);
  memset(_list,0,sizeof(*_list));
}

static void packet_copy_to_ogg(const packet_copy *_copy,ogg_packet *_op){
  _op->packet=_copy->data;
  _op->bytes=_copy->bytes;
  _op->granulepos=_copy->granulepos;
  _op->packetno=_copy->packetno;
  _op->b_o_s=_copy->b_o_s;
  _op->e_o_s=_copy->e_o_s;
}

static void fill_plane(unsigned char *_data,int _width,int _height,
 int _stride,int _frame,int _pli){
  int x;
  int y;
  for(y=0;y<_height;y++){
    for(x=0;x<_width;x++){
      _data[y*_stride+x]=(unsigned char)(x*37+y*17+_pli*67+_frame*29);
    }
  }
}

static int ilog(unsigned int _v){
  int ret;
  ret=0;
  while(_v){
    ret++;
    _v>>=1;
  }
  return ret;
}

static void encode_stream(th_pixel_fmt _pixel_fmt,packet_list *_headers,
 packet_list *_data,th_info *_info){
  th_info         info;
  th_comment      comment;
  th_enc_ctx     *enc;
  th_ycbcr_buffer yuv;
  ogg_packet      op;
  int             keyframe_frequency;
  int             frame;
  int             pli;
  int             ret;
  th_info_init(&info);
  info.frame_width=FRAME_WIDTH;
  info.frame_height=FRAME_HEIGHT;
  info.pic_width=FRAME_WIDTH-6;
  info.pic_height=FRAME_HEIGHT-12;
  info.pic_x=2;
  info.pic_y=4;
  info.fps_numerator=30;
  info.fps_denominator=1;
  info.aspect_numerator=1;
  info.aspect_denominator=1;
  info.colorspace=TH_CS_UNSPECIFIED;
  info.pixel_fmt=_pixel_fmt;
  info.quality=32;
  info.keyframe_granule_shift=ilog(4);
  enc=th_encode_alloc(&info);
  if(enc==NULL)FAIL("could not allocate encoder");
  th_comment_init(&comment);
  keyframe_frequency=4;
  ret=th_encode_ctl(enc,TH_ENCCTL_SET_KEYFRAME_FREQUENCY_FORCE,
   &keyframe_frequency,sizeof(keyframe_frequency));
  if(ret<0)FAIL("could not set keyframe frequency");
  packet_list_init(_headers);
  packet_list_init(_data);
  while((ret=th_encode_flushheader(enc,&comment,&op))>0){
    packet_list_append(_headers,&op);
  }
  if(ret<0)FAIL("could not flush encoder headers");
  for(pli=0;pli<3;pli++){
    int xshift;
    int yshift;
    xshift=pli!=0&&!(_pixel_fmt&1);
    yshift=pli!=0&&!(_pixel_fmt&2);
    yuv[pli].width=FRAME_WIDTH>>xshift;
    yuv[pli].height=FRAME_HEIGHT>>yshift;
    yuv[pli].stride=yuv[pli].width;
    yuv[pli].data=(unsigned char *)malloc(
     (size_t)yuv[pli].stride*yuv[pli].height);
    if(yuv[pli].data==NULL)FAIL("could not allocate source plane");
  }
  for(frame=0;frame<FRAME_COUNT;frame++){
    for(pli=0;pli<3;pli++){
      fill_plane(yuv[pli].data,yuv[pli].width,yuv[pli].height,
       yuv[pli].stride,frame,pli);
    }
    ret=th_encode_ycbcr_in(enc,yuv);
    if(ret<0)FAIL("could not submit source frame");
    while((ret=th_encode_packetout(enc,frame==FRAME_COUNT-1,&op))>0){
      packet_list_append(_data,&op);
    }
    if(ret<0)FAIL("could not retrieve encoded packet");
  }
  for(pli=0;pli<3;pli++)free(yuv[pli].data);
  *_info=info;
  th_comment_clear(&comment);
  th_encode_free(enc);
}

static void parse_headers(const packet_list *_headers,th_info *_info,
 th_comment *_comment,th_setup_info **_setup){
  ogg_packet op;
  int        i;
  int        ret;
  th_info_init(_info);
  th_comment_init(_comment);
  *_setup=NULL;
  for(i=0;i<_headers->npackets;i++){
    packet_copy_to_ogg(_headers->packets+i,&op);
    ret=th_decode_headerin(_info,_comment,_setup,&op);
    if(ret<=0)FAIL("could not parse Theora header");
  }
}

static void region_plane_bounds(const th_info *_info,const region_spec *_region,
 int _pli,int *_x0,int *_y0,int *_width,int *_height){
  int hdec;
  int vdec;
  int x0;
  int x1;
  int y0;
  int y1;
  hdec=_pli!=0&&!(_info->pixel_fmt&1);
  vdec=_pli!=0&&!(_info->pixel_fmt&2);
  x0=_region->x>>hdec;
  x1=(_region->x+_region->width+(1<<hdec)-1)>>hdec;
  y0=_region->y>>vdec;
  y1=(_region->y+_region->height+(1<<vdec)-1)>>vdec;
  *_x0=x0;
  *_y0=y0;
  *_width=x1-x0;
  *_height=y1-y0;
}

static void expected_layout(const th_info *_info,const region_spec *_region,
 th_region_layout *_layout){
  size_t plane_size[3];
  int    pli;
  memset(_layout,0,sizeof(*_layout));
  for(pli=0;pli<3;pli++){
    int x0;
    int y0;
    region_plane_bounds(_info,_region,pli,&x0,&y0,
     _layout->width+pli,_layout->height+pli);
    (void)x0;
    (void)y0;
    _layout->stride[pli]=_layout->width[pli];
    plane_size[pli]=(size_t)_layout->width[pli]*_layout->height[pli];
  }
  _layout->offset[0]=0;
  _layout->offset[1]=plane_size[0];
  _layout->offset[2]=plane_size[0]+plane_size[1];
  _layout->size=plane_size[0]+plane_size[1]+plane_size[2];
}

static void check_layout(const th_region_layout *_expected,
 const th_region_layout *_actual){
  int pli;
  for(pli=0;pli<3;pli++){
    if(_actual->width[pli]!=_expected->width[pli]||
     _actual->height[pli]!=_expected->height[pli]||
     _actual->stride[pli]!=_expected->stride[pli]||
     _actual->offset[pli]!=_expected->offset[pli]){
      FAIL("region layout differs from the canonical layout");
    }
  }
  if(_actual->size!=_expected->size)FAIL("region size differs");
}

static void check_region_buffer(th_dec_ctx *_dec,const th_info *_info,
 const region_spec *_region,const th_ycbcr_buffer _full){
  th_region        region;
  th_region_layout expected;
  th_region_layout actual;
  unsigned char   *storage;
  unsigned char   *dst;
  size_t           i;
  int              pli;
  int              row;
  int              ret;
  region.x=_region->x;
  region.y=_region->y;
  region.width=_region->width;
  region.height=_region->height;
  expected_layout(_info,_region,&expected);
  ret=th_decode_ycbcr_region_layout(_dec,&region,&actual);
  if(ret<0)FAIL("region layout query failed");
  check_layout(&expected,&actual);
  ret=th_decode_ycbcr_region(_dec,&region,&actual,NULL,0);
  if(ret<0)FAIL("region layout-only call failed");
  check_layout(&expected,&actual);
  storage=(unsigned char *)malloc(expected.size+2*GUARD_BYTES);
  if(storage==NULL)FAIL("out of memory allocating region buffer");
  memset(storage,0xA5,expected.size+2*GUARD_BYTES);
  dst=storage+GUARD_BYTES;
  ret=th_decode_ycbcr_region(_dec,&region,&actual,dst,expected.size);
  if(ret<0)FAIL("region copy failed");
  check_layout(&expected,&actual);
  for(i=0;i<GUARD_BYTES;i++){
    if(storage[i]!=0xA5||storage[GUARD_BYTES+expected.size+i]!=0xA5){
      FAIL("region copy wrote outside the requested buffer");
    }
  }
  for(pli=0;pli<3;pli++){
    int x0;
    int y0;
    int width;
    int height;
    region_plane_bounds(_info,_region,pli,&x0,&y0,&width,&height);
    for(row=0;row<height;row++){
      const unsigned char *ref;
      const unsigned char *got;
      ref=_full[pli].data+(ptrdiff_t)(y0+row)*_full[pli].stride+x0;
      got=dst+expected.offset[pli]+(size_t)row*expected.stride[pli];
      if(memcmp(ref,got,(size_t)width)!=0){
        FAIL("region pixels differ from the full-frame crop");
      }
      hash_bytes(got,(size_t)width);
      g_checks++;
    }
  }
  free(storage);
}

typedef struct{
  th_ycbcr_buffer  full;
  th_info          info;
  region_spec      region;
  th_region_layout layout;
  int              next_row;
  int              calls;
  int              fail;
}row_context;

static int check_row(void *_ctx,const th_region_row *_row,
 const th_region_layout *_layout){
  row_context *ctx;
  int          pli;
  ctx=(row_context *)_ctx;
  if(_row->row!=ctx->next_row)ctx->fail=1;
  check_layout(&ctx->layout,_layout);
  if(ctx->fail)return 0;
  for(pli=0;pli<3;pli++){
    const unsigned char *expected;
    const unsigned char *actual;
    int                  x0;
    int                  y0;
    int                  width;
    int                  height;
    int                  source_y;
    int                  vdec;
    region_plane_bounds(&ctx->info,&ctx->region,pli,&x0,&y0,&width,&height);
    (void)height;
    vdec=pli!=0&&!(ctx->info.pixel_fmt&2);
    source_y=(ctx->region.y+_row->row)>>vdec;
    expected=ctx->full[pli].data+(ptrdiff_t)source_y*
     ctx->full[pli].stride+x0;
    actual=pli==0?_row->y:_row->cb;
    if(pli==2)actual=_row->cr;
    if(memcmp(expected,actual,(size_t)width)!=0)ctx->fail=1;
  }
  if(ctx->fail)return 0;
  ctx->next_row++;
  ctx->calls++;
  return 0;
}

static void check_region_rows(th_dec_ctx *_dec,const th_info *_info,
 const region_spec *_region,const th_ycbcr_buffer _full){
  th_region    region;
  row_context  ctx;
  int          ret;
  memset(&ctx,0,sizeof(ctx));
  memcpy(ctx.full,_full,sizeof(ctx.full));
  ctx.info=*_info;
  ctx.region=*_region;
  expected_layout(_info,_region,&ctx.layout);
  region.x=_region->x;
  region.y=_region->y;
  region.width=_region->width;
  region.height=_region->height;
  ret=th_decode_ycbcr_region_rows(_dec,&region,check_row,&ctx);
  if(ret!=0||ctx.fail||ctx.calls!=_region->height){
    FAIL("row-oriented region output differs from the full-frame crop");
  }
}

typedef struct{
  int calls;
  int stop_after;
}abort_context;

static int abort_row(void *_ctx,const th_region_row *_row,
 const th_region_layout *_layout){
  abort_context *ctx;
  (void)_row;
  (void)_layout;
  ctx=(abort_context *)_ctx;
  ctx->calls++;
  return ctx->calls>=ctx->stop_after?77:0;
}

static void check_region_errors(th_dec_ctx *_dec,const th_info *_info){
  th_region        region;
  th_region_layout layout;
  unsigned char    byte;
  region.x=0;
  region.y=0;
  region.width=4;
  region.height=4;
  if(th_decode_ycbcr_region_layout(NULL,&region,&layout)!=TH_EFAULT){
    FAIL("layout accepted a NULL decoder");
  }
  if(th_decode_ycbcr_region_layout(_dec,NULL,&layout)!=TH_EFAULT){
    FAIL("layout accepted a NULL region");
  }
  if(th_decode_ycbcr_region_layout(_dec,&region,NULL)!=TH_EFAULT){
    FAIL("layout accepted a NULL layout");
  }
  if(th_decode_ycbcr_region(NULL,&region,&layout,NULL,0)!=TH_EFAULT){
    FAIL("region copy accepted a NULL decoder");
  }
  if(th_decode_ycbcr_region(_dec,NULL,&layout,NULL,0)!=TH_EFAULT){
    FAIL("region copy accepted a NULL region");
  }
  if(th_decode_ycbcr_region(_dec,&region,NULL,NULL,0)!=TH_EFAULT){
    FAIL("region copy accepted a NULL layout");
  }
  if(th_decode_ycbcr_region_rows(_dec,&region,NULL,NULL)!=TH_EFAULT){
    FAIL("row region output accepted a NULL callback");
  }
  region.width=0;
  if(th_decode_ycbcr_region_layout(_dec,&region,&layout)!=TH_EINVAL){
    FAIL("layout accepted a zero-width region");
  }
  region.width=-1;
  if(th_decode_ycbcr_region_layout(_dec,&region,&layout)!=TH_EINVAL){
    FAIL("layout accepted a negative-width region");
  }
  region.x=-1;
  region.width=4;
  if(th_decode_ycbcr_region_layout(_dec,&region,&layout)!=TH_EINVAL){
    FAIL("layout accepted a negative origin");
  }
  region.x=(int)_info->frame_width-3;
  region.y=0;
  region.width=4;
  region.height=1;
  if(th_decode_ycbcr_region_layout(_dec,&region,&layout)!=TH_EINVAL){
    FAIL("layout accepted a region past the right edge");
  }
  region.x=0;
  region.y=(int)_info->frame_height-3;
  region.width=4;
  region.height=4;
  if(th_decode_ycbcr_region_layout(_dec,&region,&layout)!=TH_EINVAL){
    FAIL("layout accepted a region past the bottom edge");
  }
  region.x=0;
  region.y=0;
  region.width=4;
  region.height=4;
  if(th_decode_ycbcr_region(_dec,&region,&layout,&byte,1)!=TH_EINVAL){
    FAIL("region copy accepted an undersized destination");
  }
}

static void check_linear_growth(th_dec_ctx *_dec,const th_info *_info){
  int heights[]={1,2,4,8,16};
  int i;
  for(i=0;i<(int)(sizeof(heights)/sizeof(heights[0]));i++){
    th_region        region;
    th_region_layout expected;
    th_region_layout actual;
    region_spec      spec;
    spec.x=3;
    spec.y=5;
    spec.width=17;
    spec.height=heights[i];
    expected_layout(_info,&spec,&expected);
    region.x=spec.x;
    region.y=spec.y;
    region.width=spec.width;
    region.height=spec.height;
    if(th_decode_ycbcr_region_layout(_dec,&region,&actual)<0){
      FAIL("layout query failed during growth check");
    }
    check_layout(&expected,&actual);
  }
}

static void run_consistency(th_pixel_fmt _pixel_fmt){
  packet_list   headers;
  packet_list   data;
  th_info       encoded_info;
  th_info       info;
  th_comment    comment;
  th_setup_info *setup;
  th_dec_ctx    *full_dec;
  th_dec_ctx    *region_dec;
  th_ycbcr_buffer full;
  int            packet;
  int            region;
  int            ret_full;
  int            ret_region;
  int            frames;
  th_region      query_region;
  th_region_layout query_layout;
  abort_context  abort_ctx;
  encode_stream(_pixel_fmt,&headers,&data,&encoded_info);
  parse_headers(&headers,&info,&comment,&setup);
  if(info.frame_width!=FRAME_WIDTH||info.frame_height!=FRAME_HEIGHT||
   info.pic_width!=encoded_info.pic_width||
   info.pic_height!=encoded_info.pic_height||
   info.pixel_fmt!=_pixel_fmt){
    FAIL("decoded stream info does not match the encoder");
  }
  full_dec=th_decode_alloc(&info,setup);
  region_dec=th_decode_alloc(&info,setup);
  if(full_dec==NULL||region_dec==NULL)FAIL("could not allocate decoder");
  query_region.x=0;
  query_region.y=0;
  query_region.width=4;
  query_region.height=4;
  if(th_decode_ycbcr_region_layout(region_dec,&query_region,&query_layout)<0){
    FAIL("layout query failed before decoding");
  }
  if(th_decode_ycbcr_region(region_dec,&query_region,&query_layout,NULL,0)<0){
    FAIL("layout-only region call failed before decoding");
  }
  if(th_decode_ycbcr_region_rows(region_dec,&query_region,check_row,NULL)!=
   TH_EINVAL){
    FAIL("row region output succeeded before a frame was decoded");
  }
  check_region_errors(full_dec,&info);
  check_linear_growth(full_dec,&info);
  frames=0;
  for(packet=0;packet<data.npackets;packet++){
    ogg_packet op;
    ogg_int64_t gp_full;
    ogg_int64_t gp_region;
    packet_copy_to_ogg(data.packets+packet,&op);
    ret_full=th_decode_packetin(full_dec,&op,&gp_full);
    packet_copy_to_ogg(data.packets+packet,&op);
    ret_region=th_decode_packetin(region_dec,&op,&gp_region);
    if(ret_full<0||ret_region<0)FAIL("packet decode failed");
    if(ret_full!=ret_region)FAIL("decoder return values diverged");
    if(gp_full!=gp_region)FAIL("decoder granule positions diverged");
    if(ret_full==TH_DUPFRAME)continue;
    frames++;
    if(th_decode_ycbcr_out(full_dec,full)<0){
      FAIL("could not retrieve the full decoded frame");
    }
    for(region=0;
     region<(int)(sizeof(TEST_REGIONS)/sizeof(TEST_REGIONS[0]));region++){
      check_region_buffer(region_dec,&info,TEST_REGIONS+region,full);
      check_region_rows(region_dec,&info,TEST_REGIONS+region,full);
    }
    abort_ctx.calls=0;
    abort_ctx.stop_after=2;
    {
      th_region r;
      r.x=1;
      r.y=1;
      r.width=8;
      r.height=8;
      if(th_decode_ycbcr_region_rows(region_dec,&r,abort_row,&abort_ctx)!=77){
        FAIL("row callback stop value was not returned");
      }
      if(abort_ctx.calls!=2)FAIL("row callback did not stop immediately");
    }
  }
  if(frames!=FRAME_COUNT)FAIL("wrong number of frames decoded");
  printf("region_decode: pixel_fmt=%d frames=%d regions=%d rows=%d "
   "bytes=%lu mismatches=0 hash=%016llx\n",(int)_pixel_fmt,frames,
   (int)(sizeof(TEST_REGIONS)/sizeof(TEST_REGIONS[0])),g_checks,
   (unsigned long)g_bytes,g_hash);
  th_decode_free(region_dec);
  th_decode_free(full_dec);
  th_setup_free(setup);
  th_comment_clear(&comment);
  th_info_clear(&info);
  th_info_clear(&encoded_info);
  packet_list_clear(&data);
  packet_list_clear(&headers);
}

int main(int argc,char *argv[]){
  (void)argc;
  (void)argv;
  run_consistency(TH_PF_420);
  run_consistency(TH_PF_422);
  run_consistency(TH_PF_444);
  printf("region_decode: %d rows, %lu bytes, 0 mismatches, "
   "consistent hash=%016llx\n",g_checks,(unsigned long)g_bytes,g_hash);
  return 0;
}
