#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/stat.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>
#include <jpeglib.h>
#include <setjmp.h>

#define SOURCE "ux0:data/VitaSend/received"
#define DEST "ux0:data/SleepMixer/Music"
#define MAX_FILES 256
#define MAX_PATH 512
static char files[MAX_FILES][256];
static int count=0,selected=0,step=0,number=1;
static char status[256]="Select an MP3 (X). Circle exits.";
static char destination[MAX_PATH],source[MAX_PATH];
static char existing[8][32]; static int existing_count=0;
static vita2d_pgf *font;
static void line(int y,const char *s,uint32_t color) {vita2d_pgf_draw_text(font,35,y,color,1.1f,s);}
#define RGBA(r,g,b,a) ((uint32_t)(r)|((uint32_t)(g)<<8)|((uint32_t)(b)<<16)|((uint32_t)(a)<<24))
static void label_at(int x,int y,float size,const char *s,uint32_t color){vita2d_pgf_draw_text(font,x,y,color,size,s);}
static void rect(int x,int y,int w,int h,uint32_t color){vita2d_draw_rectangle(x,y,w,h,color);}
static void trim_label(char *out,size_t cap,const char *in,size_t limit){
 size_t n=strlen(in);if(limit>=cap)limit=cap-1;
 if(n<=limit){snprintf(out,cap,"%s",in);return;}
 if(limit<5){snprintf(out,cap,"...");return;}
 memcpy(out,in,limit-3);memcpy(out+limit-3,"...",3);out[limit]=0;
}
static void panel(int x,int y,int w,int h){rect(x,y,w,h,RGBA(27,33,46,255));}
static void header(const char *subtitle){
 rect(0,0,960,544,RGBA(15,20,30,255));
 rect(0,0,960,7,RGBA(65,190,215,255));
 label_at(36,47,1.55f,"VITA COVER CONVERTER",RGBA(245,248,252,255));
 label_at(38,76,0.88f,subtitle,RGBA(154,169,185,255));
 rect(36,94,888,2,RGBA(52,65,81,255));
}
static void footer(const char *hint){
 rect(0,480,960,64,RGBA(21,27,38,255));
 rect(0,479,960,1,RGBA(52,65,81,255));
 label_at(37,505,0.85f,hint,RGBA(109,211,230,255));
 char short_status[116];trim_label(short_status,sizeof(short_status),status,110);
 label_at(37,530,0.76f,short_status,RGBA(184,195,205,255));
}
static int ends_mp3(const char *s) {size_t n=strlen(s);return n>4 && !strcasecmp(s+n-4,".mp3");}
static void scan(void) {
  count=0;selected=0;
  SceUID d=sceIoDopen(SOURCE);if(d<0){snprintf(status,sizeof(status),"Cannot open %s",SOURCE);return;}
  SceIoDirent ent;
  while(count<MAX_FILES) {memset(&ent,0,sizeof(ent));if(sceIoDread(d,&ent)<=0)break;
    if(ends_mp3(ent.d_name)){snprintf(files[count],sizeof(files[count]),"%s",ent.d_name);count++;}
  }
  sceIoDclose(d);
  snprintf(status,sizeof(status),"%d MP3 files found. X to select.",count);
}
static int next_number(void) {
  for(int n=1;n<1000000;n++){char p[MAX_PATH];SceIoStat st;
    snprintf(p,sizeof(p),DEST "/Track%04d",n);
    if(sceIoGetstat(p,&st)<0)return n;
  }return -1;
}
static int copy_file(const char *a,const char *b) {
  FILE *in=fopen(a,"rb");if(!in)return -1;
  FILE *out=fopen(b,"wb");if(!out){fclose(in);return -1;}
  char buf[32768];size_t n;int ok=0;
  while((n=fread(buf,1,sizeof(buf),in))>0)if(fwrite(buf,1,n,out)!=n){ok=-1;break;}
  if(ferror(in)||fflush(out))ok=-1;
  if(fclose(out))ok=-1;
  fclose(in);if(ok)remove(b);return ok;
}
static uint32_t synchsafe(const unsigned char *p) {
 return ((uint32_t)(p[0]&127)<<21)|((uint32_t)(p[1]&127)<<14)|((uint32_t)(p[2]&127)<<7)|(p[3]&127);
}
static uint32_t be32(const unsigned char *p) {
 return ((uint32_t)p[0]<<24)|((uint32_t)p[1]<<16)|((uint32_t)p[2]<<8)|p[3];
}
/* Extracts JPEG APIC from ID3v2.3/v2.4. Unsupported PNG artwork is rejected safely. */
static int artwork(const char *mp3,const char *out) {
 FILE *f=fopen(mp3,"rb");if(!f)return -1;
 unsigned char h[10];if(fread(h,1,10,f)!=10||memcmp(h,"ID3",3)||h[3]<3||h[3]>4){fclose(f);return -2;}
 int ver=h[3];uint32_t size=synchsafe(h+6);
 if(size>16*1024*1024||size<10||h[5]&0x80){fclose(f);return -2;}
 unsigned char *data=malloc(size);if(!data){fclose(f);return -1;}
 if(fread(data,1,size,f)!=size){free(data);fclose(f);return -1;}fclose(f);
 int result=-2;
 for(size_t pos=0;pos+10<=size;) {
  unsigned char *p=data+pos;
  if(!p[0])break;
  uint32_t len=ver==4?synchsafe(p+4):be32(p+4);
  if(len>size-pos-10)break;
  if(!memcmp(p,"APIC",4)&&len>12&&!(p[9]&0x0c)) {
   unsigned char *q=p+10,*end=q+len;
   int enc=*q++;unsigned char *mime=q;
   while(q<end&&*q)q++;
   if(q>=end)break;
   int jpeg=(!strcmp((char*)mime,"image/jpeg")||!strcmp((char*)mime,"image/jpg"));
   q++;if(q>=end)break;q++; /* picture type */
   if(enc==0||enc==3){while(q<end&&*q)q++;if(q<end)q++;}
   else {while(q+1<end&&(q[0]||q[1]))q+=2;q+=2;}
   if(q+2<=end&&jpeg&&q[0]==0xff&&q[1]==0xd8) {
    FILE *o=fopen(out,"wb");if(!o){result=-1;break;}
    size_t bytes=(size_t)(end-q);
    int wrote=(fwrite(q,1,bytes,o)==bytes); int closed=(fclose(o)==0); result=(wrote&&closed)?0:-1;
    if(result)remove(out);
   }break;
  }
  pos+=10+len;
 }
 free(data);return result;
}

typedef struct {struct jpeg_error_mgr pub;jmp_buf jump;} JpegError;
static void jpeg_fail(j_common_ptr c){JpegError *e=(JpegError*)c->err;longjmp(e->jump,1);}
static int convert_cover(const char *input,const char *output){
 FILE *in=fopen(input,"rb");if(!in)return -1;
 struct jpeg_decompress_struct dec;JpegError err;unsigned char *pixels=NULL,*canvas=NULL;int ok=-1;
 memset(&dec,0,sizeof(dec));dec.err=jpeg_std_error(&err.pub);err.pub.error_exit=jpeg_fail;
 if(setjmp(err.jump)){goto done;}
 jpeg_create_decompress(&dec);jpeg_stdio_src(&dec,in);jpeg_read_header(&dec,TRUE);
 if(!dec.image_width||!dec.image_height||dec.image_width>4096||dec.image_height>4096){goto done;}
 dec.out_color_space=JCS_RGB;jpeg_start_decompress(&dec);
 size_t w=dec.output_width,h=dec.output_height;
 if(w>4096||h>4096||w*h>16000000){goto done;}
 pixels=malloc(w*h*3);canvas=calloc(256*256,3);
 if(!pixels||!canvas)goto done;
 while(dec.output_scanline<h){JSAMPROW row=pixels+(size_t)dec.output_scanline*w*3;jpeg_read_scanlines(&dec,&row,1);}
 jpeg_finish_decompress(&dec);jpeg_destroy_decompress(&dec);memset(&dec,0,sizeof(dec));fclose(in);in=NULL;
 int dw=256,dh=256;
 if(w>=h)dh=(int)(h*256/w);else dw=(int)(w*256/h);
 if(dw<1)dw=1;if(dh<1)dh=1;
 for(int y=0;y<dh;y++)for(int x=0;x<dw;x++){
  size_t sx=(size_t)x*w/dw,sy=(size_t)y*h/dh;
  size_t src=(sy*w+sx)*3,dst=(((256-dh)/2+y)*256+(256-dw)/2+x)*3;
  memcpy(canvas+dst,pixels+src,3);
 }
 FILE *out=fopen(output,"wb");if(!out)goto done;
 struct jpeg_compress_struct enc;JpegError e2;memset(&enc,0,sizeof(enc));enc.err=jpeg_std_error(&e2.pub);e2.pub.error_exit=jpeg_fail;
 if(setjmp(e2.jump)){fclose(out);remove(output);goto done;}
 jpeg_create_compress(&enc);jpeg_stdio_dest(&enc,out);
 enc.image_width=256;enc.image_height=256;enc.input_components=3;enc.in_color_space=JCS_RGB;
 jpeg_set_defaults(&enc);jpeg_set_quality(&enc,88,TRUE);jpeg_start_compress(&enc,TRUE);
 while(enc.next_scanline<256){JSAMPROW row=canvas+(size_t)enc.next_scanline*256*3;jpeg_write_scanlines(&enc,&row,1);}
 jpeg_finish_compress(&enc);jpeg_destroy_compress(&enc);
 ok=fclose(out)==0?0:-1;if(ok)remove(output);
 done:
 if(dec.mem)jpeg_destroy_decompress(&dec);
 if(in)fclose(in);free(pixels);free(canvas);return ok;
}

static void prepare(void){
 number=next_number();if(number<0){strcpy(status,"No available folder number");return;}
 snprintf(source,sizeof(source),SOURCE "/%s",files[selected]);
 snprintf(destination,sizeof(destination),DEST "/Track%04d",number);
 existing_count=0; SceUID d=sceIoDopen(DEST); if(d>=0){SceIoDirent e;while(existing_count<8){memset(&e,0,sizeof(e));if(sceIoDread(d,&e)<=0)break;if(!strncmp(e.d_name,"Track",5)){snprintf(existing[existing_count],32,"%s",e.d_name);existing_count++;}}sceIoDclose(d);}
 step=1;snprintf(status,sizeof(status),"Confirm with X; Circle to cancel.");
}
static void save(void){
 sceIoMkdir("ux0:data/SleepMixer",0777);sceIoMkdir(DEST,0777);
 if(sceIoMkdir(destination,0777)<0){strcpy(status,"Folder exists or cannot be created");step=0;return;}
 char mp3[MAX_PATH],cover[MAX_PATH],raw[MAX_PATH];snprintf(mp3,sizeof(mp3),"%s/%s",destination,files[selected]);
 snprintf(cover,sizeof(cover),"%s/cover.jpg",destination);
 snprintf(raw,sizeof(raw),"%s/.artwork.tmp",destination);
 if(copy_file(source,mp3)){strcpy(status,"MP3 copy failed");sceIoRmdir(destination);step=0;return;}
 int art=artwork(source,raw);
 if(!art)art=convert_cover(raw,cover);
 remove(raw);
 if(art){remove(mp3);sceIoRmdir(destination);snprintf(status,sizeof(status),"Artwork missing/unsupported (JPEG ID3v2.3/2.4 only)");step=0;return;}
 snprintf(status,sizeof(status),"Saved Track%04d with cover.jpg and MP3",number);
 step=0;
}
int main(void){
 sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
 vita2d_init();font=vita2d_load_default_pgf();scan();
 unsigned old=0;
 while(1){
  SceCtrlData pad;memset(&pad,0,sizeof(pad));sceCtrlPeekBufferPositive(0,&pad,1);
  unsigned pressed=pad.buttons&~old;old=pad.buttons;
  if(step==0){
   if((pressed&SCE_CTRL_DOWN)&&selected<count-1)selected++;
   if((pressed&SCE_CTRL_UP)&&selected>0)selected--;
   if((pressed&SCE_CTRL_CROSS)&&count)prepare();
   if(pressed&SCE_CTRL_CIRCLE)break;
  }else{
   if(pressed&SCE_CTRL_CIRCLE)step=0;
   if(pressed&SCE_CTRL_CROSS)save();
  }
  vita2d_start_drawing();vita2d_clear_screen();
  header(step?"02  /  CONFIRM DESTINATION":"01  /  SELECT MP3");
  if(!step){
   panel(35,112,890,348);
   label_at(53,141,0.86f,"SOURCE  ux0:data/VitaSend/received",RGBA(163,181,196,255));
   label_at(845,141,0.85f,"MP3",RGBA(109,211,230,255));
   rect(52,151,856,1,RGBA(53,64,79,255));
   if(!count)label_at(56,215,1.0f,"No MP3 files found in the source folder.",RGBA(230,235,242,255));
   int first=selected-4;if(first<0)first=0;
   if(first>count-8)first=count-8;if(first<0)first=0;
   for(int j=0;j<8&&first+j<count;j++){
    int i=first+j,y=158+j*36;
    if(i==selected){rect(48,y,864,33,RGBA(37,80,98,255));rect(48,y,4,33,RGBA(100,218,238,255));}
    char name[100];trim_label(name,sizeof(name),files[i],75);
    label_at(65,y+23,0.91f,name,i==selected?RGBA(255,255,255,255):RGBA(190,201,214,255));
   }
   char counter[60];snprintf(counter,sizeof(counter),"%d FILES   |   %d / %d",count,count?selected+1:0,count);
   label_at(710,448,0.79f,counter,RGBA(154,169,185,255));
   footer("UP / DOWN  Browse      X  Select      O  Exit");
  }else{
   panel(35,112,890,348);
   label_at(55,145,0.9f,"SELECTED TRACK",RGBA(109,211,230,255));
   char track[100];trim_label(track,sizeof(track),files[selected],76);
   label_at(55,172,1.04f,track,RGBA(248,250,252,255));
   rect(52,188,856,1,RGBA(53,64,79,255));
   label_at(55,219,0.86f,"SAVE LOCATION",RGBA(109,211,230,255));
   label_at(55,245,0.94f,DEST,RGBA(235,240,246,255));
   char folder[40];snprintf(folder,sizeof(folder),"Track%04d",number);
   rect(54,260,852,43,RGBA(36,76,89,255));
   label_at(69,288,1.1f,folder,RGBA(255,255,255,255));
   label_at(55,333,0.84f,"EXISTING TRACK FOLDERS",RGBA(160,179,196,255));
   if(!existing_count)label_at(55,363,0.87f,"No existing folders found",RGBA(183,192,203,255));
   for(int k=0;k<existing_count&&k<8;k++){
    int x=55+(k%4)*214,y=365+(k/4)*35;
    label_at(x,y,0.85f,existing[k],RGBA(220,228,237,255));
   }
   label_at(55,446,0.8f,"MP3 + cover.jpg  |  No source files changed",RGBA(157,174,190,255));
   footer("X  Confirm and save      O  Back");
  }
  vita2d_end_drawing();vita2d_swap_buffers();
 }
 vita2d_free_pgf(font);vita2d_fini();sceKernelExitProcess(0);return 0;
}
