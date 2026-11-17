#include <psp2/ctrl.h>
#include <psp2/io/dirent.h>
#include <psp2/io/fcntl.h>
#include <psp2/kernel/processmgr.h>
#include <vita2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>

#define SOURCE "ux0:data/VitaSend/received"
#define DEST "ux0:data/SleepMixer/Music"
#define MAX_FILES 256
#define MAX_PATH 512
static char files[MAX_FILES][256];
static int count=0,selected=0,step=0,number=1;
static char status[256]="Select an MP3 (X). Circle exits.";
static char destination[MAX_PATH],source[MAX_PATH];\nstatic char existing[8][32]; static int existing_count=0;
static vita2d_pgf *font;
static void line(int y,const char *s,uint32_t color) {vita2d_pgf_draw_text(font,35,y,color,1.1f,s);}
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
static void prepare(void){
 number=next_number();if(number<0){strcpy(status,"No available folder number");return;}
 snprintf(source,sizeof(source),SOURCE "/%s",files[selected]);
 snprintf(destination,sizeof(destination),DEST "/Track%04d",number);
 existing_count=0; SceUID d=sceIoDopen(DEST); if(d>=0){SceIoDirent e;while(existing_count<8){memset(&e,0,sizeof(e));if(sceIoDread(d,&e)<=0)break;if(!strncmp(e.d_name,"Track",5)){snprintf(existing[existing_count],32,"%s",e.d_name);existing_count++;}}sceIoDclose(d);}\n step=1;snprintf(status,sizeof(status),"Confirm with X; Circle to cancel.");
}
static void save(void){
 sceIoMkdir("ux0:data/SleepMixer",0777);sceIoMkdir(DEST,0777);
 if(sceIoMkdir(destination,0777)<0){strcpy(status,"Folder exists or cannot be created");step=0;return;}
 char mp3[MAX_PATH],cover[MAX_PATH];snprintf(mp3,sizeof(mp3),"%s/%s",destination,files[selected]);
 snprintf(cover,sizeof(cover),"%s/cover.jpg",destination);
 if(copy_file(source,mp3)){strcpy(status,"MP3 copy failed");sceIoRmdir(destination);step=0;return;}
 int art=artwork(source,cover);
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
  line(50,"VITA COVER CONVERTER",0xffffffff);
  line(86,"Source: " SOURCE,0xffbbbbbb);
  if(!step){
   for(int i=selected-3<0?0:selected-3,j=0;i<count&&j<9;i++,j++){
    char row[320];snprintf(row,sizeof(row),"%c %s",i==selected?'>':' ',files[i]);
    line(130+j*32,row,i==selected?0xff55ddff:0xffffffff);
   }
  }else{
   line(145,"Destination preview:",0xff55ddff);
   line(180,DEST,0xffffffff);
   line(215,destination,0xffffffff);
   line(260,"Existing folders (up to 8):",0xffdddddd);\n   for(int k=0;k<existing_count;k++)line(286+k*21,existing[k],0xffaaaaaa);
   
   line(460,"X Confirm / Circle Cancel",0xff55ddff);
  }
  line(480,status,0xffdddddd);
  vita2d_end_drawing();vita2d_swap_buffers();
 }
 vita2d_free_pgf(font);vita2d_fini();sceKernelExitProcess(0);return 0;
}
