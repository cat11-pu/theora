/*Diagnostic decoder for the TPK1 packet container used by the malformed
   stream regression corpus.

  Usage: decode_tpk FILE
  Prints one line per packet with the return code and, on failure, the byte
   offset inside the packet at which the decoder detected the error.*/
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ogg/ogg.h>
#include <theora/theoradec.h>

static uint32_t rd32(const unsigned char *p){
  return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|
   (uint32_t)p[3]<<24;
}

int main(int argc,char **argv){
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
  int failures;
  if(argc!=2){
    fprintf(stderr,"usage: %s FILE.tpk\n",argv[0]);
    return 2;
  }
  f=fopen(argv[1],"rb");
  if(f==NULL)return 2;
  fseek(f,0,SEEK_END);
  size=ftell(f);
  fseek(f,0,SEEK_SET);
  if(size<8)return 2;
  data=(unsigned char *)malloc((size_t)size);
  if(data==NULL||fread(data,1,(size_t)size,f)!=(size_t)size)return 2;
  fclose(f);
  if(memcmp(data,"TPK1",4)!=0)return 2;
  npackets=rd32(data+4);
  packets=(unsigned char **)calloc(npackets,sizeof(*packets));
  sizes=(uint32_t *)calloc(npackets,sizeof(*sizes));
  if(packets==NULL||sizes==NULL)return 2;
  {
    size_t off;
    off=8;
    for(i=0;i<npackets;i++){
      if(off+4>(size_t)size)return 2;
      sizes[i]=rd32(data+off);
      off+=4;
      if(off+sizes[i]>(size_t)size)return 2;
      packets[i]=data+off;
      off+=sizes[i];
    }
  }
  th_info_init(&info);
  th_comment_init(&comment);
  setup=NULL;
  dec=NULL;
  header_done=0;
  failures=0;
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
      printf("packet=%u stage=header bytes=%u ret=%d offset=%zu\n",
       i,sizes[i],ret,err_off);
      if(ret<0){
        failures++;
        break;
      }
      if(ret>0)continue;
      dec=th_decode_alloc(&info,setup);
      if(dec==NULL){
        fprintf(stderr,"th_decode_alloc failed\n");
        failures++;
        break;
      }
      header_done=1;
    }
    ret=th_decode_packetin_ex(dec,&op,&gp,&err_off);
    printf("packet=%u stage=frame bytes=%u ret=%d offset=%zu gp=%lld\n",
     i,sizes[i],ret,err_off,(long long)gp);
    if(ret<0)failures++;
  }
  if(dec!=NULL)th_decode_free(dec);
  if(setup!=NULL)th_setup_free(setup);
  th_comment_clear(&comment);
  th_info_clear(&info);
  free(sizes);
  free(packets);
  free(data);
  return failures!=0;
}
