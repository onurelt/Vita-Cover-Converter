#include "artwork.h"
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>
#include <png.h>
#include <jpeglib.h>

/* Limit ID3 allocation and decoded image memory on the Vita. */
#define TAG_LIMIT (16u * 1024u * 1024u)
#define COVER_LIMIT (8u * 1024u * 1024u)
#define MAX_SIDE 4096u

static uint32_t be24(const unsigned char *p) {
  return ((uint32_t)p[0]<<16)|((uint32_t)p[1]<<8)|p[2];
}
static uint32_t be32(const unsigned char *p) {
  return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
static uint32_t safe32(const unsigned char *p) {
  return ((uint32_t)(p[0]&127)<<21)|((uint32_t)(p[1]&127)<<14)|((uint32_t)(p[2]&127)<<7)|(p[3]&127);
}
static int parse_image(const unsigned char *p, size_t len, int *png) {
  static const unsigned char psig[8]={137,80,78,71,13,10,26,10};
  if(len>=8&&!memcmp(p,psig,8)){*png=1;return 1;}
  if(len>=3&&p[0]==255&&p[1]==216&&p[2]==255){*png=0;return 1;}
  return 0;
}
int vcc_extract_artwork(const char *mp3, const char *output, int *is_png) {
  unsigned char head[10],*tag=NULL;int rc=-2;FILE *f=fopen(mp3,"rb");
  if(!f)return -1;
  if(fread(head,1,10,f)!=10||memcmp(head,"ID3",3)||head[3]<2||head[3]>4){goto done;}
  int v=head[3];
  /* Unsynchronised/compressed tags cannot safely be copied byte-for-byte. */
  if(head[5]&0x80){goto done;}
  uint32_t size=safe32(head+6);
  if(size<6||size>TAG_LIMIT){rc=-1;goto done;}
  tag=malloc(size);if(!tag){rc=-1;goto done;}
  if(fread(tag,1,size,f)!=size){rc=-1;goto done;}
  size_t pos=0;
  if(head[5]&0x40){
    if(v==3&&size>=4)pos=4u+be32(tag);
    else if(v==4&&size>=4)pos=safe32(tag);
    else {goto done;}
    if(pos>size){rc=-1;goto done;}
  }
  while(pos<size){
    int hdr=v==2?6:10;
    if(size-pos<(size_t)hdr||!tag[pos])break;
    unsigned char *frame=tag+pos;
    uint32_t n=v==2?be24(frame+3):(v==4?safe32(frame+4):be32(frame+4));
    if(n>size-pos-(size_t)hdr){rc=-1;break;}
    int apic=(v==2?!memcmp(frame,"PIC",3):!memcmp(frame,"APIC",4));
    int flags=(v==2?0:(frame[hdr-1]|frame[hdr-2]));
    if(apic&&n>8&&!(flags&0x0c)){
      const unsigned char *p=frame+hdr,*end=p+n;
      int enc=*p++;
      if(v==2){if(end-p<4)break;p+=3;} /* ID3v2.2 3-byte format */
      else {while(p<end&&*p)p++;if(p==end)break;p++;}
      if(p>=end)break;
      p++; /* picture type */
      if(enc==0||enc==3){while(p<end&&*p)p++;if(p<end)p++;}
      else if(enc==1||enc==2){
        if(enc==1&&end-p>=2&&((p[0]==255&&p[1]==254)||(p[0]==254&&p[1]==255)))p+=2;
        while(end-p>=2&&(p[0]||p[1]))p+=2;
        if(end-p>=2)p+=2;
      }else break;
      if(p<end&&(size_t)(end-p)<=COVER_LIMIT){
        int png=0;
        if(parse_image(p,(size_t)(end-p),&png)){
          FILE *out=fopen(output,"wb");
          if(!out){rc=-1;break;}
          int ok=fwrite(p,1,(size_t)(end-p),out)==(size_t)(end-p);
          if(fclose(out)!=0)ok=0;
          if(ok){*is_png=png;rc=0;}else{remove(output);rc=-1;}
          break;
        }
      }
    }
    pos+=(size_t)hdr+n;
  }
done:
  free(tag);fclose(f);return rc;
}

typedef struct {struct jpeg_error_mgr base; jmp_buf jump;} JErr;
static void jpeg_error(j_common_ptr ptr){longjmp(((JErr*)ptr->err)->jump,1);}
int vcc_png_to_jpeg(const char *input, const char *output){
  png_image pic;unsigned char *src=NULL,*dst=NULL;FILE *out=NULL;int ok=-1;
  memset(&pic,0,sizeof(pic));pic.version=PNG_IMAGE_VERSION;
  if(!png_image_begin_read_from_file(&pic,input))return -1;
  unsigned w=pic.width,h=pic.height;
  if(!w||!h||w>MAX_SIDE||h>MAX_SIDE||((uint64_t)w*h>16000000u))goto done;
  pic.format=PNG_FORMAT_RGB;
  src=malloc(PNG_IMAGE_SIZE(pic));dst=calloc(256u*256u,3);
  if(!src||!dst||!png_image_finish_read(&pic,NULL,src,0,NULL))goto done;
  int dw=256,dh=256;
  if(w>=h)dh=(int)((uint64_t)h*256/w);else dw=(int)((uint64_t)w*256/h);
  if(dw<1)dw=1;if(dh<1)dh=1;
  for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){
    size_t sx=(size_t)x*w/dw,sy=(size_t)y*h/dh;
    size_t a=(sy*w+sx)*3,b=((size_t)((256-dh)/2+y)*256+(256-dw)/2+x)*3;
    memcpy(dst+b,src+a,3);
  }
  out=fopen(output,"wb");if(!out)goto done;
  struct jpeg_compress_struct enc;JErr j;
  memset(&enc,0,sizeof(enc));enc.err=jpeg_std_error(&j.base);j.base.error_exit=jpeg_error;
  if(setjmp(j.jump)){jpeg_destroy_compress(&enc);goto done;}
  jpeg_create_compress(&enc);jpeg_stdio_dest(&enc,out);
  enc.image_width=256;enc.image_height=256;enc.input_components=3;enc.in_color_space=JCS_RGB;
  jpeg_set_defaults(&enc);jpeg_set_quality(&enc,88,TRUE);jpeg_start_compress(&enc,TRUE);
  while(enc.next_scanline<256){JSAMPROW row=dst+(size_t)enc.next_scanline*256*3;jpeg_write_scanlines(&enc,&row,1);}
  jpeg_finish_compress(&enc);jpeg_destroy_compress(&enc);
  ok=fclose(out)==0?0:-1;out=NULL;
done:
  if(out)fclose(out);
  if(ok)remove(output);
  png_image_free(&pic);free(src);free(dst);return ok;
}
