/********************************************************************
 *                                                                  *
 * THIS FILE IS PART OF THE OggTheora SOFTWARE CODEC SOURCE CODE.   *
 * USE, DISTRIBUTION AND REPRODUCTION OF THIS LIBRARY SOURCE IS     *
 * GOVERNED BY A BSD-STYLE SOURCE LICENSE INCLUDED WITH THIS SOURCE *
 * IN 'COPYING'. PLEASE READ THESE TERMS BEFORE DISTRIBUTING.       *
 *                                                                  *
 * THE Theora SOURCE CODE IS COPYRIGHT (C) 2002-2009                *
 * by the Xiph.Org Foundation and contributors                      *
 * https://www.xiph.org/                                            *
 *                                                                  *
 ********************************************************************

  function:

 ********************************************************************/
#include <string.h>
#include "internal.h"

/*Branchless clamp to the 8-bit range.
  This produces the identical byte as OC_CLAMP255 for every signed 16-bit
   argument (the full range of an iDCT residue plus an 8-bit predictor), but
   the two min/max selects compile to conditional moves instead of the macro's
   sign-mask arithmetic, and need no memory table or temporary buffer.*/
static inline unsigned char oc_clamp255_fast(int _x){
  _x=_x<0?0:_x;
  _x=_x>255?255:_x;
  return (unsigned char)_x;
}

void oc_frag_copy_c(unsigned char *_dst,const unsigned char *_src,int _ystride){
  int i;
  for(i=8;i-->0;){
    memcpy(_dst,_src,8*sizeof(*_dst));
    _dst+=_ystride;
    _src+=_ystride;
  }
}

/*Copies the fragments specified by the lists of fragment indices from one
   frame to another.
  _dst_frame:     The reference frame to copy to.
  _src_frame:     The reference frame to copy from.
  _ystride:       The row stride of the reference frames.
  _fragis:        A pointer to a list of fragment indices.
  _nfragis:       The number of fragment indices to copy.
  _frag_buf_offs: The offsets of fragments in the reference frames.*/
void oc_frag_copy_list_c(unsigned char *_dst_frame,
 const unsigned char *_src_frame,int _ystride,
 const ptrdiff_t *_fragis,ptrdiff_t _nfragis,const ptrdiff_t *_frag_buf_offs){
  ptrdiff_t fragii;
  for(fragii=0;fragii<_nfragis;fragii++){
    ptrdiff_t frag_buf_off;
    frag_buf_off=_frag_buf_offs[_fragis[fragii]];
    oc_frag_copy_c(_dst_frame+frag_buf_off,
     _src_frame+frag_buf_off,_ystride);
  }
}

void oc_frag_recon_intra_c(unsigned char *_dst,int _ystride,
 const ogg_int16_t _residue[64]){
  int i;
  for(i=0;i<8;i++){
    const ogg_int16_t *r=_residue+(i<<3);
    /*Fully unrolled 8-pixel row: no inner-loop counter and no per-pixel
       address arithmetic, which lets the compiler schedule the clamps back
       to back without a temporary row buffer.*/
    _dst[0]=oc_clamp255_fast(r[0]+128);
    _dst[1]=oc_clamp255_fast(r[1]+128);
    _dst[2]=oc_clamp255_fast(r[2]+128);
    _dst[3]=oc_clamp255_fast(r[3]+128);
    _dst[4]=oc_clamp255_fast(r[4]+128);
    _dst[5]=oc_clamp255_fast(r[5]+128);
    _dst[6]=oc_clamp255_fast(r[6]+128);
    _dst[7]=oc_clamp255_fast(r[7]+128);
    _dst+=_ystride;
  }
}

void oc_frag_recon_inter_c(unsigned char *_dst,
 const unsigned char *_src,int _ystride,const ogg_int16_t _residue[64]){
  int i;
  for(i=0;i<8;i++){
    const ogg_int16_t *r=_residue+(i<<3);
    _dst[0]=oc_clamp255_fast(r[0]+_src[0]);
    _dst[1]=oc_clamp255_fast(r[1]+_src[1]);
    _dst[2]=oc_clamp255_fast(r[2]+_src[2]);
    _dst[3]=oc_clamp255_fast(r[3]+_src[3]);
    _dst[4]=oc_clamp255_fast(r[4]+_src[4]);
    _dst[5]=oc_clamp255_fast(r[5]+_src[5]);
    _dst[6]=oc_clamp255_fast(r[6]+_src[6]);
    _dst[7]=oc_clamp255_fast(r[7]+_src[7]);
    _dst+=_ystride;
    _src+=_ystride;
  }
}

void oc_frag_recon_inter2_c(unsigned char *_dst,const unsigned char *_src1,
 const unsigned char *_src2,int _ystride,const ogg_int16_t _residue[64]){
  int i;
  for(i=0;i<8;i++){
    const ogg_int16_t *r=_residue+(i<<3);
    /*(_src1+_src2)>>1 is the VP3 rounded average: it matches the MMX/ARM
       kernels (16-bit add followed by an arithmetic shift right by one).*/
    _dst[0]=oc_clamp255_fast(r[0]+((_src1[0]+_src2[0])>>1));
    _dst[1]=oc_clamp255_fast(r[1]+((_src1[1]+_src2[1])>>1));
    _dst[2]=oc_clamp255_fast(r[2]+((_src1[2]+_src2[2])>>1));
    _dst[3]=oc_clamp255_fast(r[3]+((_src1[3]+_src2[3])>>1));
    _dst[4]=oc_clamp255_fast(r[4]+((_src1[4]+_src2[4])>>1));
    _dst[5]=oc_clamp255_fast(r[5]+((_src1[5]+_src2[5])>>1));
    _dst[6]=oc_clamp255_fast(r[6]+((_src1[6]+_src2[6])>>1));
    _dst[7]=oc_clamp255_fast(r[7]+((_src1[7]+_src2[7])>>1));
    _dst+=_ystride;
    _src1+=_ystride;
    _src2+=_ystride;
  }
}

void oc_restore_fpu_c(void){}
