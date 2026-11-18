/*Regression test for malformed Theora packet handling.

  The legal baseline must decode without an error. Every malformed sample must
  be rejected with a negative return code and a byte offset no larger than the
  packet that failed.*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ogg/ogg.h>
#include <theora/theoradec.h>

#ifndef SAMPLEDIR
# define SAMPLEDIR "samples"
#endif

static uint32_t rd32(const unsigned char *p){
  return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|
   (uint32_t)p[3]<<24;
}

static int decode_file(const char *path,int expect_error,
 const char **stage,uint32_t *packet,uint32_t *packet_bytes,size_t *offset,
 int *error_code){
  unsigned char *data;
  unsigned char **packets;
  uint32_t *sizes;
  uint32_t npackets;
  uint32_t i;
  FILE *f;
  long size;
  th_info info;
  th_comment comment;
  th_setup_info *setup;
  th_dec_ctx *dec;
  int header_done;
  int found_error;
  int result;
  *stage="none";
  *packet=0;
  *packet_bytes=0;
  *offset=0;
  *error_code=0;
  f=fopen(path,"rb");
  if(f==NULL)return -1;
  fseek(f,0,SEEK_END);
  size=ftell(f);
  fseek(f,0,SEEK_SET);
  if(size<8||size>16*1024*1024){
    fclose(f);
    return -1;
  }
  data=(unsigned char *)malloc((size_t)size);
  if(data==NULL||fread(data,1,(size_t)size,f)!=(size_t)size){
    fclose(f);
    free(data);
    return -1;
  }
  fclose(f);
  if(memcmp(data,"TPK1",4)!=0){
    free(data);
    return -1;
  }
  npackets=rd32(data+4);
  packets=(unsigned char **)calloc(npackets,sizeof(*packets));
  sizes=(uint32_t *)calloc(npackets,sizeof(*sizes));
  if(packets==NULL||sizes==NULL){
    free(packets);
    free(sizes);
    free(data);
    return -1;
  }
  {
    size_t off;
    result=0;
    off=8;
    for(i=0;i<npackets;i++){
      if(off+4>(size_t)size){
        result=-1;
        break;
      }
      sizes[i]=rd32(data+off);
      off+=4;
      if(off+sizes[i]>(size_t)size){
        result=-1;
        break;
      }
      packets[i]=data+off;
      off+=sizes[i];
    }
    if(result<0){
      free(sizes);
      free(packets);
      free(data);
      return -1;
    }
  }
  th_info_init(&info);
  th_comment_init(&comment);
  setup=NULL;
  dec=NULL;
  header_done=0;
  found_error=0;
  for(i=0;i<npackets;i++){
    ogg_packet op;
    ogg_int64_t gp;
    size_t err_off;
    int ret;
    memset(&op,0,sizeof(op));
    op.packet=packets[i];
    op.bytes=sizes[i];
    op.b_o_s=i==0;
    gp=-1;
    err_off=0;
    if(!header_done){
      ret=th_decode_headerin_ex(&info,&comment,&setup,&op,&err_off);
      if(ret<0){
        *stage="header";
        *packet=i;
        *packet_bytes=sizes[i];
        *offset=err_off;
        *error_code=ret;
        found_error=1;
        break;
      }
      if(ret>0)continue;
      dec=th_decode_alloc(&info,setup);
      if(dec==NULL){
        *stage="alloc";
        *packet=i;
        *packet_bytes=sizes[i];
        *error_code=TH_EFAULT;
        found_error=1;
        break;
      }
      header_done=1;
    }
    ret=th_decode_packetin_ex(dec,&op,&gp,&err_off);
    if(ret<0){
      size_t ctl_off;
      ctl_off=0;
      if(th_decode_ctl(dec,TH_DECCTL_GET_ERROR_OFFSET,&ctl_off,
       sizeof(ctl_off))<0||ctl_off!=err_off){
        ret=TH_EFAULT;
      }
      *stage="frame";
      *packet=i;
      *packet_bytes=sizes[i];
      *offset=err_off;
      *error_code=ret;
      found_error=1;
      break;
    }
  }
  if(dec!=NULL)th_decode_free(dec);
  if(setup!=NULL)th_setup_free(setup);
  th_comment_clear(&comment);
  th_info_clear(&info);
  free(sizes);
  free(packets);
  free(data);
  if(expect_error)return found_error?0:-1;
  /*A legal stream may finish with a dropped-frame packet, which is a positive
     success code, but it must never be rejected.*/
  return found_error?-1:0;
}

int main(void){
  static const struct{
    const char *name;
    int         expect_error;
  } cases[]={
    {"valid.tpk",0},
    {"truncated_frame.tpk",1},
    {"bad_quant_nbms.tpk",1},
    {"bad_quant_bmi.tpk",1},
    {"bad_quant_truncated.tpk",1},
    {"bad_mv_truncated.tpk",1}
  };
  unsigned i;
  int failures;
  failures=0;
  for(i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
    char path[4096];
    const char *stage;
    uint32_t packet;
    uint32_t packet_bytes;
    size_t offset;
    int error_code;
    int ret;
    snprintf(path,sizeof(path),"%s/%s",SAMPLEDIR,cases[i].name);
    ret=decode_file(path,cases[i].expect_error,&stage,&packet,&packet_bytes,
     &offset,&error_code);
    if(ret<0){
      printf("FAIL %-28s expected=%s\n",cases[i].name,
       cases[i].expect_error?"reject":"accept");
      failures++;
    }
    else if(cases[i].expect_error){
      if(offset==0||offset>packet_bytes){
        printf("FAIL %-28s bad offset=%zu packet_bytes=%u\n",cases[i].name,
         offset,packet_bytes);
        failures++;
      }
      else{
        printf("PASS %-28s stage=%s packet=%u offset=%zu error=%d\n",
         cases[i].name,stage,packet,offset,error_code);
      }
    }
    else{
      printf("PASS %-28s decoded\n",cases[i].name);
    }
  }
  return failures!=0;
}
