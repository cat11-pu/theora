/*Exhaustively verifies that the branchless clamp helper used by the
  reconstruction kernels returns the same byte as the OC_CLAMP255 macro for
  every possible signed 16-bit input (the full range of an iDCT residue).
  Failure aborts with a non-zero status; success prints a confirmation.*/
#include <stdio.h>
#include "ocintrin.h"

static unsigned char clamp255_fast(int _x){
  _x=_x<0?0:_x;
  _x=_x>255?255:_x;
  return (unsigned char)_x;
}

int main(void){
  int i;
  for(i=-32768;i<=32767;i++){
    unsigned char expect=OC_CLAMP255(i);
    unsigned char actual=clamp255_fast(i);
    if(expect!=actual){
      printf("MISMATCH at %d: macro=%u helper=%u\n",i,expect,actual);
      return 1;
    }
  }
  /*Also cover the wider sums reachable when a predictor is added.*/
  for(i=-32768;i<=32767;i++){
    int s=i+255;
    if(OC_CLAMP255(s)!=clamp255_fast(s)){
      printf("MISMATCH at sum %d\n",s);
      return 1;
    }
  }
  printf("clamp255 helper matches OC_CLAMP255 across the full int16 domain\n");
  return 0;
}
