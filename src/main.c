#include <psp2/ctrl.h>
#include <psp2/touch.h>
#include <psp2/ime_dialog.h>
#include <psp2/sysmodule.h>
#include <psp2/common_dialog.h>
#include "artwork.h"
#include "core.h"
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
static unsigned char is_folder[MAX_FILES];
static char browse_dir[MAX_PATH]=SOURCE,folder_name[64];
static vita2d_texture *preview=NULL;
static char preview_path[MAX_PATH];
static int preview_png=0,keyboard=0;
static SceWChar16 initial_name[64],typed_name[64];
static SceTouchPanelInfo touch_info;
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
static int is_dir(const SceIoDirent *e){return SCE_S_ISDIR(e->d_stat.st_mode);}
static void scan(void){
 count=selected=0;
 SceUID d=sceIoDopen(browse_dir);
 if(d<0){snprintf(status,sizeof(status),"Cannot open %s",browse_dir);return;}
 SceIoDirent e;
 while(count<MAX_FILES){
  memset(&e,0,sizeof(e));if(sceIoDread(d,&e)<=0)break;
  if(e.d_name[0]=='.')continue;
  int dir=is_dir(&e);
  if(!dir&&!ends_mp3(e.d_name))continue;
  snprintf(files[count],sizeof(files[count]),"%s",e.d_name);
  is_folder[count++]=dir;
 }
 sceIoDclose(d);
 for(int i=0;i<count;i++)for(int j=i+1;j<count;j++)
  if(is_folder[j]>is_folder[i]||(is_folder[j]==is_folder[i]&&strcasecmp(files[j],files[i])<0)){
   char tmp[256];memcpy(tmp,files[i],256);memcpy(files[i],files[j],256);memcpy(files[j],tmp,256);
   unsigned char b=is_folder[i];is_folder[i]=is_folder[j];is_folder[j]=b;
  }
 snprintf(status,sizeof(status),"%d items found. X to open. Triangle to refresh.",count);
}
static void update_destination(void){snprintf(destination,sizeof(destination),"%s/%s",DEST,folder_name);}
static int next_number(void){
 int max=0;SceUID d=sceIoDopen(DEST);SceIoDirent e;
 if(d>=0){
  while(1){
   memset(&e,0,sizeof(e));if(sceIoDread(d,&e)<=0)break;
   if(!is_dir(&e))continue;
   int n=vcc_track_number(e.d_name);if(n>max)max=n;
  }
  sceIoDclose(d);
 }
 return max<999999?max+1:-1;
}
static void scan_existing(void){
 existing_count=0;int top[8]={0};SceUID d=sceIoDopen(DEST);SceIoDirent e;
 if(d<0)return;
 while(1){
  memset(&e,0,sizeof(e));if(sceIoDread(d,&e)<=0)break;
  if(!is_dir(&e))continue;
  int n=vcc_track_number(e.d_name);if(n<1)continue;
  for(int k=0;k<8;k++)if(n>top[k]){
   for(int j=7;j>k;j--)top[j]=top[j-1];top[k]=n;break;
  }
 }
 sceIoDclose(d);
 for(int k=0;k<8&&top[k];k++){snprintf(existing[k],32,"Track%04d",top[k]);existing_count++;}
}
static void adjust_number(int delta){
 if(number+delta<1||number+delta>999999)return;
 number+=delta;snprintf(folder_name,sizeof(folder_name),"Track%04d",number);update_destination();
 SceIoStat st;
 if(sceIoGetstat(destination,&st)>=0)strcpy(status,"Folder exists. Choose another.");
 else snprintf(status,sizeof(status),"Destination: %s",folder_name);
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
/* Artwork extraction is implemented in artwork.c. */
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

static void reset_preview(void){
 if(preview){vita2d_free_texture(preview);preview=NULL;}
 if(preview_path[0]){remove(preview_path);preview_path[0]=0;}
}
static void go_back(void){
 if(!strcmp(browse_dir,SOURCE))return;
 char *p=strrchr(browse_dir,'/');
 if(!p||p<=browse_dir+strlen(SOURCE))strcpy(browse_dir,SOURCE);
 else *p=0;
 scan();
}
static void prepare(void){
 if(count==0)return;
 if(is_folder[selected]){
  char next[MAX_PATH];
  if(snprintf(next,sizeof(next),"%s/%s",browse_dir,files[selected])>=(int)sizeof(next)){
   strcpy(status,"Path too long.");return;
  }
  snprintf(browse_dir,sizeof(browse_dir),"%s",next);scan();return;
 }
 sceIoMkdir("ux0:data/VitaCoverConverter",0777);
 sceIoMkdir("ux0:data/SleepMixer",0777);
 sceIoMkdir(DEST,0777);
 number=next_number();
 if(number<0){strcpy(status,"No available Track number.");return;}
 snprintf(folder_name,sizeof(folder_name),"Track%04d",number);
 update_destination();scan_existing();
 if(snprintf(source,sizeof(source),"%s/%s",browse_dir,files[selected])>=(int)sizeof(source)){
  strcpy(status,"MP3 path too long.");return;
 }
 reset_preview();
 int fmt=0;
 const char *tmp="ux0:data/VitaCoverConverter/artwork.tmp";
 int rc=vcc_extract_artwork(source,tmp,&fmt);
 if(rc){remove(tmp);snprintf(status,sizeof(status),rc==-2?
      "No embedded JPEG/PNG artwork in this MP3.":"Could not read the MP3 artwork.");return;}
 preview_png=fmt;snprintf(preview_path,sizeof(preview_path),"%s",tmp);
 preview=fmt?vita2d_load_PNG_file(tmp):vita2d_load_JPEG_file(tmp);
 if(!preview){reset_preview();strcpy(status,"Embedded image cannot be displayed.");return;}
 step=1;strcpy(status,"Review the cover and folder before saving.");
}
static void rename_folder(void){
 memset(initial_name,0,sizeof(initial_name));memset(typed_name,0,sizeof(typed_name));
 for(int i=0;i<48&&folder_name[i];i++)initial_name[i]=(unsigned char)folder_name[i];
 SceImeDialogParam p;sceImeDialogParamInit(&p);
 p.supportedLanguages=0;p.languagesForced=SCE_FALSE;
 p.type=SCE_IME_TYPE_DEFAULT;
 p.textBoxMode=SCE_IME_DIALOG_TEXTBOX_MODE_WITH_CLEAR;
 p.title=u"Folder name";
 p.maxTextLength=48;p.initialText=initial_name;p.inputTextBuffer=typed_name;
 int rc=sceImeDialogInit(&p);
 if(rc>=0)keyboard=1;
 else snprintf(status,sizeof(status),"Vita keyboard unavailable: %08X",(unsigned)rc);
}
static void poll_keyboard(void){
 if(!keyboard||sceImeDialogGetStatus()!=SCE_COMMON_DIALOG_STATUS_FINISHED)return;
 SceImeDialogResult result;memset(&result,0,sizeof(result));
 sceImeDialogGetResult(&result);sceImeDialogTerm();keyboard=0;
 if(result.button!=SCE_IME_DIALOG_BUTTON_ENTER)return;
 char new_name[64]={0};int valid=1;
 for(int i=0;i<48&&typed_name[i];i++){
  if(typed_name[i]>127){valid=0;break;}
  new_name[i]=(char)typed_name[i];
 }
 if(!valid||!vcc_valid_folder(new_name)){
  strcpy(status,"Name must use 1-48 letters/numbers/spaces, - or _.");return;
 }
 snprintf(folder_name,sizeof(folder_name),"%s",new_name);update_destination();
 SceIoStat st;
 if(sceIoGetstat(destination,&st)>=0)strcpy(status,"That folder already exists. Rename again.");
 else snprintf(status,sizeof(status),"Destination renamed: %s",folder_name);
}
static void save(void){
 if(!preview||!vcc_valid_folder(folder_name)){strcpy(status,"Missing cover or invalid name.");return;}
 sceIoMkdir("ux0:data/SleepMixer",0777);sceIoMkdir(DEST,0777);
 SceIoStat st;
 if(sceIoGetstat(destination,&st)>=0){strcpy(status,"Folder already exists. Nothing overwritten.");return;}
 if(sceIoMkdir(destination,0777)<0){strcpy(status,"Cannot create destination folder.");return;}
 char mp3[MAX_PATH],partial[MAX_PATH],cover[MAX_PATH],partial_cover[MAX_PATH];
 int a=snprintf(mp3,sizeof(mp3),"%s/%s",destination,files[selected]);
 int b=snprintf(partial,sizeof(partial),"%s/.copy.part",destination);
 int c=snprintf(cover,sizeof(cover),"%s/cover.jpg",destination);
 int d=snprintf(partial_cover,sizeof(partial_cover),"%s/.cover.part",destination);
 if(a<0||b<0||c<0||d<0||a>=(int)sizeof(mp3)||b>=(int)sizeof(partial)||
    c>=(int)sizeof(cover)||d>=(int)sizeof(partial_cover)){
  sceIoRmdir(destination);strcpy(status,"Output path too long.");return;
 }
 int ok=copy_file(source,partial)==0;
 if(ok)ok=(preview_png?vcc_png_to_jpeg(preview_path,partial_cover):
                          convert_cover(preview_path,partial_cover))==0;
 if(ok)ok=sceIoRename(partial,mp3)>=0;
 if(ok)ok=sceIoRename(partial_cover,cover)>=0;
 if(!ok){
  remove(partial);remove(partial_cover);remove(mp3);remove(cover);
  sceIoRmdir(destination);
  strcpy(status,"Save failed; cleaned incomplete destination.");return;
 }
 snprintf(status,sizeof(status),"Saved %s with MP3 and cover.jpg.",folder_name);
 reset_preview();step=0;
}
static void draw_preview(int x,int y,int box){
 rect(x,y,box,box,RGBA(30,47,60,255));
 if(!preview)return;
 float w=vita2d_texture_get_width(preview),h=vita2d_texture_get_height(preview);
 if(w<1||h<1)return;
 float scale=box/w;if(h*scale>box)scale=box/h;
 vita2d_draw_texture_scale(preview,x+(box-w*scale)/2,y+(box-h*scale)/2,scale,scale);
}
static int list_first(void){
 int first=selected-3;if(first<0)first=0;
 if(first>count-7)first=count-7;if(first<0)first=0;return first;
}
static void draw_browser(void){
 header("01  /  SELECT MP3");
 panel(35,111,555,352);
 label_at(54,141,0.88f,"MP3 FILES",RGBA(94,210,227,255));
 char short_path[85];trim_label(short_path,sizeof(short_path),browse_dir,52);
 label_at(54,165,0.79f,short_path,RGBA(160,177,192,255));
 rect(52,179,520,1,RGBA(53,68,83,255));
 if(!count)label_at(58,237,1.0f,"No MP3 files found. Triangle to refresh.",RGBA(230,239,245,255));
 int first=list_first();
 for(int j=0;j<7&&first+j<count;j++){
  int i=first+j,y=188+j*36;
  if(i==selected){rect(49,y,525,34,RGBA(35,81,99,255));rect(49,y,4,34,RGBA(74,219,234,255));}
  char name[90],row[110];trim_label(name,sizeof(name),files[i],47);
  snprintf(row,sizeof(row),"%s %s",is_folder[i]?"[DIR]":" MP3 ",name);
  label_at(59,y+24,0.91f,row,i==selected?RGBA(254,255,255,255):RGBA(188,202,215,255));
 }
 panel(606,111,318,352);
 label_at(632,146,0.89f,"COVER ARTWORK",RGBA(94,210,227,255));
 rect(632,173,267,224,RGBA(27,44,57,255));
 label_at(667,287,1.08f,"MP3  TO  COVER",RGBA(193,211,222,255));
 label_at(630,429,0.81f,"Select a track for a preview",RGBA(163,179,192,255));
 footer("UP/DOWN  Browse      X  Open/Preview      O  Back      TRIANGLE  Refresh");
}
static void draw_review(void){
 header("02  /  REVIEW");
 panel(35,111,889,352);
 draw_preview(56,132,286);
 char short_name[110];trim_label(short_name,sizeof(short_name),files[selected],35);
 label_at(57,447,0.81f,short_name,RGBA(232,241,248,255));
 label_at(377,144,0.9f,"DESTINATION FOLDER",RGBA(96,210,228,255));
 rect(377,165,511,70,RGBA(38,56,70,255));
 label_at(396,210,1.36f,folder_name,RGBA(255,255,255,255));
 label_at(835,207,0.78f,"EDIT",RGBA(102,221,233,255));
 label_at(379,261,0.85f,"ux0:data/SleepMixer/Music/",RGBA(184,195,207,255));
 label_at(378,296,0.85f,"EXISTING TRACK FOLDERS",RGBA(156,174,190,255));
 if(!existing_count)label_at(378,328,0.80f,"No existing Track folders",RGBA(159,177,190,255));
 for(int k=0;k<existing_count&&k<8;k++){
  int x=378+(k%4)*127,y=310+(k/4)*32;
  rect(x,y,119,27,RGBA(39,58,72,255));
  label_at(x+7,y+19,0.72f,existing[k],RGBA(225,233,241,255));
 }
 rect(377,386,511,54,RGBA(25,115,135,255));
 label_at(526,423,1.25f,"CONFIRM & SAVE",RGBA(255,255,255,255));
 footer("X  Confirm      O  Back      TRIANGLE  Rename      L/R  Change number");
}
static void touch_action(int x,int y){
 if(!step){
  if(x>=49&&x<575&&y>=188&&y<440){
   int row=(y-188)/36+list_first();
   if(row>=0&&row<count){if(row==selected)prepare();else selected=row;}
  }else if(y>480&&x>750)scan();
 }else{
  if(x>=377&&x<890&&y>=165&&y<=235)rename_folder();
  else if(x>=377&&x<890&&y>=386&&y<=440)save();
 }
}
int main(void){
 sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
 sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);
 memset(&touch_info,0,sizeof(touch_info));
 sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&touch_info);
 sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
 vita2d_init();vita2d_set_clear_color(RGBA(14,21,31,255));
 font=vita2d_load_default_pgf();scan();
 unsigned old=0;int was_touch=0;
 while(1){
  SceCtrlData pad;memset(&pad,0,sizeof(pad));sceCtrlPeekBufferPositive(0,&pad,1);
  unsigned pressed=pad.buttons&~old;old=pad.buttons;
  if(!keyboard){
   if(step==0){
    if((pressed&SCE_CTRL_DOWN)&&selected<count-1)selected++;
    if((pressed&SCE_CTRL_UP)&&selected>0)selected--;
    if(pressed&SCE_CTRL_CROSS)prepare();
    if(pressed&SCE_CTRL_TRIANGLE)scan();
    if(pressed&SCE_CTRL_CIRCLE){
     if(!strcmp(browse_dir,SOURCE))break;
     go_back();
    }
   }else{
    if(pressed&SCE_CTRL_CIRCLE){step=0;reset_preview();}
    else if(pressed&SCE_CTRL_CROSS)save();
    else if(pressed&SCE_CTRL_TRIANGLE)rename_folder();
    else if(pressed&SCE_CTRL_LTRIGGER)adjust_number(-1);
    else if(pressed&SCE_CTRL_RTRIGGER)adjust_number(1);
   }
   SceTouchData t;memset(&t,0,sizeof(t));
   sceTouchPeek(SCE_TOUCH_PORT_FRONT,&t,1);
   if(t.reportNum&&!was_touch){
    int dx=touch_info.maxDispX-touch_info.minDispX;
    int dy=touch_info.maxDispY-touch_info.minDispY;
    int x=dx>0?(t.report[0].x-touch_info.minDispX)*960/dx:t.report[0].x/2;
    int y=dy>0?(t.report[0].y-touch_info.minDispY)*544/dy:t.report[0].y/2;
    touch_action(x,y);
   }
   was_touch=t.reportNum>0;
  }else{poll_keyboard();was_touch=1;}
  vita2d_start_drawing();vita2d_clear_screen();
  if(step)draw_review();else draw_browser();
  vita2d_end_drawing();
  if(keyboard)vita2d_common_dialog_update();
  vita2d_swap_buffers();
 }
 if(keyboard)sceImeDialogTerm();
 reset_preview();vita2d_free_pgf(font);vita2d_fini();
 sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);
 sceKernelExitProcess(0);return 0;
}
