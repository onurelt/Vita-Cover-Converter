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
#include <psp2/kernel/threadmgr.h>
#include <vita2d.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>
#include <sys/stat.h>
#include <jpeglib.h>
#include <setjmp.h>
#include <stdatomic.h>
#include <math.h>

#define SOURCE "ux0:data/VitaSend/received"
#define DEST "ux0:data/SleepMixer/Music"
#define MAX_FILES 256
#define MAX_PATH 512
static char files[MAX_FILES][256];
static unsigned char is_folder[MAX_FILES];
static char browse_dir[MAX_PATH]=SOURCE,folder_name[64];
static vita2d_texture *preview=NULL,*background=NULL,*brand_icon=NULL;
static char preview_path[MAX_PATH];
static int preview_png=0,keyboard=0;
static SceWChar16 initial_name[64],typed_name[64];
static SceTouchPanelInfo touch_info;
static int count=0,selected=0,step=0,number=1;
static char status[256]="Select an MP3 (X). Circle exits.";
static char destination[MAX_PATH],source[MAX_PATH];
static char existing[8][32]; static int existing_count=0;
static vita2d_pgf *font;
static float animation_time=0.0f,selection_anim=0.0f,scroll_anim=0.0f;
static float page_anim=1.0f,cover_anim=0.0f,draw_offset=0.0f;
static int animated_step=-1;
enum { BROWSER, REVIEW, SAVING, SAVED };
enum { CREATE_FOLDER, COPY_TRACK, CONVERT_COVER, FINISH_SAVE };
static atomic_int save_stage;
static SceUID save_thread=-1;
static int save_ok;
#define RGBA(r,g,b,a) ((uint32_t)(r)|((uint32_t)(g)<<8)|((uint32_t)(b)<<16)|((uint32_t)(a)<<24))
static void label_at(int x,int y,float size,const char *s,uint32_t color){vita2d_pgf_draw_text(font,x,y+(int)draw_offset,color,size,s);}
static void rect(int x,int y,int w,int h,uint32_t color){vita2d_draw_rectangle(x,y+(int)draw_offset,w,h,color);}
static void fitted(int x,int y,int width,float size,const char *s,uint32_t color);
static const uint32_t ink=RGBA(246,250,253,255), teal=RGBA(83,220,233,255);
static const uint32_t muted=RGBA(178,196,208,255), surface=RGBA(15,29,43,235);
static const uint32_t surface_lift=RGBA(26,45,61,246), rule=RGBA(98,129,147,125);
static const uint32_t action=RGBA(13,116,139,245), shadow=RGBA(0,7,16,150);
static void rounded(int x,int y,int w,int h,int r,uint32_t c){
 rect(x,y+r,w,h-2*r,c);
 for(int dy=0;dy<r;dy++){
  int inset=r-(int)sqrtf((float)(r*r-(r-dy)*(r-dy)));
  rect(x+inset,y+dy,w-2*inset,1,c);
  rect(x+inset,y+h-1-dy,w-2*inset,1,c);
 }
}
static void fitted(int x,int y,int width,float size,const char *s,uint32_t color){
 char buf[512];
 snprintf(buf,sizeof(buf),"%s",s);
 if(vita2d_pgf_text_width(font,size,buf)>width){
  size_t n=strlen(buf);
  if(n>sizeof(buf)-4)n=sizeof(buf)-4;
  while(n){
   do{n--;}while(n&&((unsigned char)buf[n]&0xc0)==0x80);
   memcpy(buf+n,"...",4);
   if(vita2d_pgf_text_width(font,size,buf)<=width)break;
  }
 }
 label_at(x,y,size,buf,color);
}
static void panel(int x,int y,int w,int h){
 rounded(x+2,y+5,w,h,18,shadow);
 rounded(x,y,w,h,18,surface);
}
static void header(const char *subtitle){
 rounded(32,14,42,42,12,shadow);
 if(brand_icon)vita2d_draw_texture_scale(brand_icon,33,15+(int)draw_offset,0.31f,0.31f);
 else {
  vita2d_draw_fill_circle(53,35+(int)draw_offset,15,teal);
  vita2d_draw_fill_circle(53,35+(int)draw_offset,5,surface);
 }
 label_at(88,35,1.08f,"VITA COVER STUDIO",ink);
 label_at(89,55,0.60f,"SLEEPMIXER ARTWORK PREP",muted);
 rounded(766,16,160,34,12,surface_lift);
 fitted(778,39,136,0.72f,subtitle,teal);
 rect(34,70,892,1,rule);
}
static void footer(const char *hint){
 rounded(31,484,898,48,13,shadow);
 rounded(31,481,898,48,13,surface);
 vita2d_draw_fill_circle(48,495+(int)draw_offset,3,teal);
 fitted(60,501,852,0.73f,hint,ink);
 fitted(44,522,870,0.67f,status,muted);
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
   int n=vcc_track_number(e.d_name);if(n>max&&n<=99999)max=n;
  }
  sceIoDclose(d);
 }
 return max<99999?max+1:-1;
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
 for(int k=0;k<8&&top[k];k++){snprintf(existing[k],32,"Track%05d",top[k]);existing_count++;}
}
static void adjust_number(int delta){
 if(number+delta<1||number+delta>99999)return;
 number+=delta;snprintf(folder_name,sizeof(folder_name),"Track%05d",number);update_destination();
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
static void draw_browser(void);
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
 snprintf(folder_name,sizeof(folder_name),"Track%05d",number);
 update_destination();scan_existing();
 if(snprintf(source,sizeof(source),"%s/%s",browse_dir,files[selected])>=(int)sizeof(source)){
  strcpy(status,"MP3 path too long.");return;
 }
 reset_preview();
 strcpy(status,"Reading embedded cover artwork…");
 vita2d_start_drawing();
 vita2d_clear_screen();
 if(background)vita2d_draw_texture(background,0,0);
 draw_browser();
 vita2d_end_drawing();
 vita2d_swap_buffers();
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
 p.title=u"Track number (Track#####)";
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
  strcpy(status,"Use exactly Track plus five digits, for example Track00041.");return;
 }
 snprintf(folder_name,sizeof(folder_name),"%s",new_name);
 number=vcc_track_number(folder_name);update_destination();
 SceIoStat st;
 if(sceIoGetstat(destination,&st)>=0)strcpy(status,"That folder already exists. Rename again.");
 else snprintf(status,sizeof(status),"Destination renamed: %s",folder_name);
}
static int save_worker(SceSize args,void *arg){
 (void)args;
 (void)arg;
 save_ok=0;
 sceIoMkdir("ux0:data/SleepMixer",0777);sceIoMkdir(DEST,0777);
 SceIoStat st;
 if(sceIoGetstat(destination,&st)>=0){strcpy(status,"Folder already exists. Choose another name.");return 0;}
 if(sceIoMkdir(destination,0777)<0){strcpy(status,"Cannot create destination folder. Check storage.");return 0;}
 char mp3[MAX_PATH],partial[MAX_PATH],cover[MAX_PATH],partial_cover[MAX_PATH];
 int a=snprintf(mp3,sizeof(mp3),"%s/%s",destination,files[selected]);
 int b=snprintf(partial,sizeof(partial),"%s/.copy.part",destination);
 int c=snprintf(cover,sizeof(cover),"%s/cover.jpg",destination);
 int d=snprintf(partial_cover,sizeof(partial_cover),"%s/.cover.part",destination);
 if(a<0||b<0||c<0||d<0||a>=(int)sizeof(mp3)||b>=(int)sizeof(partial)||
    c>=(int)sizeof(cover)||d>=(int)sizeof(partial_cover)){
  sceIoRmdir(destination);strcpy(status,"Output path too long.");return 0;
 }
 const char *failure="Could not copy the MP3. Check free space and source file.";
 atomic_store(&save_stage,COPY_TRACK);
 int ok=copy_file(source,partial)==0;
 if(ok){
  failure="Could not convert the embedded artwork to JPEG.";
  atomic_store(&save_stage,CONVERT_COVER);
  ok=(preview_png?vcc_png_to_jpeg(preview_path,partial_cover):convert_cover(preview_path,partial_cover))==0;
 }
 if(ok){
  failure="Could not finalize the destination files.";
  atomic_store(&save_stage,FINISH_SAVE);
  ok=sceIoRename(partial,mp3)>=0 && sceIoRename(partial_cover,cover)>=0;
 }
 if(!ok){
  remove(partial);remove(partial_cover);remove(mp3);remove(cover);
  int cleanup=sceIoRmdir(destination);
  snprintf(status,sizeof(status),"%s%s",failure,cleanup<0?" Incomplete folder may remain.":"");
  return 0;
 }
 save_ok=1;
 return 0;
}
static void start_save(void){
 if(!preview||!vcc_valid_folder(folder_name)){strcpy(status,"Missing cover or invalid name.");return;}
 atomic_store(&save_stage,CREATE_FOLDER);
 save_thread=sceKernelCreateThread("VCC Save",save_worker,0x70,0x20000,0,0,NULL);
 if(save_thread<0||sceKernelStartThread(save_thread,0,NULL)<0){
  if(save_thread>=0)sceKernelDeleteThread(save_thread);
  save_thread=-1;
  strcpy(status,"Could not start conversion. Try again.");
  return;
 }
 step=SAVING;
}
static void poll_save(void){
 SceUInt timeout=0;
 if(sceKernelWaitThreadEnd(save_thread,NULL,&timeout)<0)return;
 sceKernelDeleteThread(save_thread);
 save_thread=-1;
 step=save_ok?SAVED:REVIEW;
 if(save_ok)strcpy(status,"Your MP3 and converted cover are ready in Sleep Mixer.");
}
static float scroll_destination(void){
 float last=count>8?(float)(count-8):0.0f;
 float target=(float)selected-3.0f;
 if(target<0.0f)target=0.0f;
 if(target>last)target=last;
 return target;
}
static void update_animation(void){
 animation_time+=0.016667f;
 if(animated_step!=step){animated_step=step;page_anim=0.0f;}
 if(page_anim<1.0f){page_anim+=0.075f;if(page_anim>1.0f)page_anim=1.0f;}
 float eased=1.0f-powf(1.0f-page_anim,3.0f);
 draw_offset=9.0f*(1.0f-eased);
 selection_anim+=(selected-selection_anim)*0.24f;
 scroll_anim+=(scroll_destination()-scroll_anim)*0.19f;
 if(fabsf(selection_anim-selected)<0.01f)selection_anim=(float)selected;
 if(fabsf(scroll_anim-scroll_destination())<0.01f)scroll_anim=scroll_destination();
 if(step==REVIEW)cover_anim+=(1.0f-cover_anim)*0.22f;
 else cover_anim=0.0f;
}
static void draw_backdrop(void){
 if(background)vita2d_draw_texture(background,0,0);
 else vita2d_draw_rectangle(0,0,960,544,RGBA(8,17,29,255));
}
static void music_glyph(int x,int y,uint32_t color){
 vita2d_draw_rectangle(x+10,y,4,18,color);
 vita2d_draw_rectangle(x+10,y,12,3,color);
 vita2d_draw_fill_circle(x+7,y+20,5,color);
 vita2d_draw_fill_circle(x+18,y+17,5,color);
}
static void folder_glyph(int x,int y,uint32_t color){
 rounded(x,y+4,22,15,4,color);
 rounded(x+2,y,10,7,3,color);
}
static void draw_preview(int x,int y,int box){
 rounded(x-4,y-4,box+8,box+8,16,RGBA(92,224,239,150));
 rounded(x,y,box,box,13,RGBA(6,16,29,255));
 if(!preview)return;
 float w=vita2d_texture_get_width(preview),h=vita2d_texture_get_height(preview);
 if(w<1||h<1)return;
 float scale=box/w;if(h*scale>box)scale=box/h;
 float reveal=0.96f+0.04f*(1.0f-powf(1.0f-cover_anim,2.0f));
 scale*=reveal;
 vita2d_draw_texture_scale(preview,x+(box-w*scale)/2,y+(box-h*scale)/2+(int)draw_offset,scale,scale);
}
static void draw_browser(void){
 header("01  /  LIBRARY");
 panel(31,91,555,376);
 label_at(53,119,0.91f,"YOUR MUSIC",teal);
 rounded(497,101,69,25,9,surface_lift);
 char total[32];snprintf(total,sizeof(total),"%03d",count);
 fitted(514,119,40,0.70f,total,ink);
 fitted(53,143,504,0.72f,browse_dir,muted);
 rect(51,151,514,1,rule);
 if(!count){
  vita2d_draw_fill_circle(308,256+(int)draw_offset,31,RGBA(24,53,69,230));
  music_glyph(293,240+(int)draw_offset,teal);
  label_at(202,318,1.02f,"No MP3 files found",ink);
  fitted(94,345,425,0.78f,"Copy MP3 files into the VitaSend received folder.",muted);
 }else{
  int first=(int)scroll_anim;
  int last=first+9;if(last>=count)last=count-1;
  float highlight=154.0f+(selection_anim-scroll_anim)*38.0f;
  float pulse=0.5f+0.5f*sinf(animation_time*3.4f);
  rounded(48,(int)highlight,522,35,10,RGBA(11,126,149,(int)(205+18*pulse)));
  rounded(48,(int)highlight,4,35,2,teal);
  for(int i=first;i<=last;i++){
   float fy=154.0f+((float)i-scroll_anim)*38.0f;
   if(fy<153.0f||fy>449.0f)continue;
   int y=(int)fy;
   if(is_folder[i])folder_glyph(63,y+7,i==selected?ink:muted);
   else music_glyph(64,y+6,i==selected?ink:teal);
   fitted(98,y+25,442,0.88f,files[i],i==selected?ink:muted);
   if(i==selected){
    vita2d_draw_fill_circle(554,y+17+(int)draw_offset,3,RGBA(239,251,255,255));
   }
  }
  if(count>8){
   float max_scroll=(float)(count-8);
   int thumb=(int)(284.0f*8.0f/count);if(thumb<23)thumb=23;
   int track_y=157+(int)(scroll_anim/max_scroll*(284-thumb));
   rounded(574,157,3,284,2,RGBA(116,146,162,110));
   rounded(574,track_y,3,thumb,2,teal);
  }
 }
 panel(601,91,328,376);
 label_at(622,120,0.91f,"COVER PREVIEW",teal);
 rounded(624,135,282,235,15,RGBA(7,18,31,230));
 if(brand_icon){
  float pulse=0.98f+0.02f*sinf(animation_time*1.7f);
  vita2d_draw_texture_scale(brand_icon,681,157+(int)draw_offset,1.39f*pulse,1.39f*pulse);
 }
 vita2d_draw_fill_circle(765,303+(int)draw_offset,5,RGBA(83,220,233,190));
 label_at(647,402,0.96f,"Artwork to Sleep Mixer",ink);
 fitted(632,427,264,0.73f,"Choose an MP3 to load its cover",muted);
 footer("UP / DOWN  Browse     X  Preview     O  Back     △  Refresh");
}
static void draw_review(void){
 header(step==REVIEW?"02  /  REVIEW":step==SAVING?"03  /  SAVING":"04  /  COMPLETE");
 panel(31,91,355,376);
 label_at(52,119,0.83f,"EMBEDDED ALBUM COVER",teal);
 draw_preview(64,132,289);
 fitted(51,445,318,0.78f,files[selected],ink);
 label_at(51,462,0.65f,"ORIGINAL MP3  ·  SOURCE PRESERVED",muted);
 panel(405,91,524,376);
 if(step==SAVING){
  static const char *const stages[]={"Create Track folder","Copy original MP3","Convert cover to JPEG","Finish and verify files"};
  int current=atomic_load(&save_stage);if(current<0)current=0;if(current>3)current=3;
  label_at(430,119,0.84f,"PREPARING YOUR TRACK",teal);
  fitted(430,157,468,1.18f,folder_name,ink);
  label_at(430,181,0.74f,DEST "/",muted);
  rounded(430,197,471,6,3,RGBA(90,128,148,115));
  float completed=(float)current/4.0f;
  if(completed>0.0f)rounded(430,197,(int)(471.0f*completed),6,3,teal);
  for(int i=0;i<4;i++){
   int y=218+i*52;
   rounded(430,y,471,43,11,i==current?surface_lift:surface);
   vita2d_draw_fill_circle(452,y+21,7,i<current?teal:i==current?RGBA(76,215,232,255):RGBA(103,130,146,190));
   if(i<current){
    rect(449,y+20,3,5,RGBA(11,34,47,255));
    rect(451,y+22,7,3,RGBA(11,34,47,255));
   }
   fitted(471,y+27,368,0.88f,stages[i],i<=current?ink:muted);
   if(i==current){
    float theta=animation_time*4.2f;
    vita2d_draw_fill_circle(872+(int)(6*cosf(theta)),y+21+(int)(6*sinf(theta)),2,teal);
   }else if(i<current)label_at(853,y+27,0.64f,"DONE",teal);
  }
  footer("Keep the app open while the MP3 and cover are being saved.");
  return;
 }
 if(step==SAVED){
  float pulse=0.5f+0.5f*sinf(animation_time*2.5f);
  vita2d_draw_fill_circle(469,137+(int)draw_offset,(int)(20+2*pulse),RGBA(30,154,150,235));
  rect(461,135,4,11,ink);rect(464,142,11,4,ink);rect(470,135,4,8,ink);
  label_at(501,143,1.04f,"Conversion complete",ink);
  label_at(430,184,0.80f,"NEW FOLDER",teal);
  fitted(430,220,468,1.19f,folder_name,ink);
  fitted(430,246,468,0.75f,DEST "/",muted);
  rect(430,263,471,1,rule);
  label_at(430,294,0.79f,"FILES READY",teal);
  fitted(430,325,468,0.82f,files[selected],ink);
  label_at(430,350,0.81f,"cover.jpg  ·  256 × 256",muted);
  rounded(430,388,471,57,14,action);
  label_at(565,423,1.02f,"Convert another",ink);
  footer("X / O  Done     Your MP3 and cover are ready to use.");
  return;
 }
 label_at(430,119,0.84f,"DESTINATION FOLDER",teal);
 rounded(430,134,471,61,13,surface_lift);
 fitted(449,173,382,1.18f,folder_name,ink);
 rounded(834,146,56,36,10,RGBA(13,116,139,240));
 label_at(847,170,0.71f,"EDIT",ink);
 label_at(430,219,0.78f,"SAVED UNDER",muted);
 fitted(430,241,471,0.90f,DEST,ink);
 rect(430,255,471,1,rule);
 label_at(430,282,0.77f,"RECENT TRACK FOLDERS",muted);
 if(!existing_count)label_at(430,318,0.76f,"Your first converted track will appear here.",muted);
 for(int k=0;k<existing_count&&k<8;k++){
  int x=430+(k%4)*118,y=291+(k/4)*35;
  rounded(x,y,110,28,8,surface_lift);
  fitted(x+7,y+19,96,0.65f,existing[k],RGBA(224,237,243,255));
 }
 rounded(430,388,471,57,14,action);
 label_at(561,423,1.02f,"Confirm & save",ink);
 footer("X  Confirm     O  Back     △  Edit Track number     L / R  Change number");
}
static void finish_review(void){
 vita2d_wait_rendering_done();
 reset_preview();
 step=BROWSER;
 strcpy(status,"Select an MP3 to preview its cover.");
}
static void touch_action(int x,int y){
 if(!step){
  if(x>=49&&x<568&&y>=154&&y<457){
   int row=(int)((float)(y-154)/38.0f+scroll_anim);
   if(row>=0&&row<count){if(row==selected)prepare();else selected=row;}
  }else if(y<75&&x>760)scan();
 }else if(step==REVIEW){
  if(x>=430&&x<900&&y>=134&&y<196)rename_folder();
  else if(x>=430&&x<901&&y>=388&&y<445)start_save();
 }else if(step==SAVED&&x>=430&&x<901&&y>=388&&y<445){
  finish_review();
 }
}
int main(void){
 sceCtrlSetSamplingMode(SCE_CTRL_MODE_DIGITAL);
 sceTouchSetSamplingState(SCE_TOUCH_PORT_FRONT,SCE_TOUCH_SAMPLING_STATE_START);
 memset(&touch_info,0,sizeof(touch_info));
 sceTouchGetPanelInfo(SCE_TOUCH_PORT_FRONT,&touch_info);
 sceSysmoduleLoadModule(SCE_SYSMODULE_IME);
 vita2d_init();vita2d_set_clear_color(RGBA(8,17,29,255));
 background=vita2d_load_JPEG_file("app0:data/app_background.jpg");
 brand_icon=vita2d_load_PNG_file("app0:sce_sys/icon0.png");
 font=vita2d_load_default_pgf();scan();
 selection_anim=0.0f;scroll_anim=0.0f;
 unsigned old=0;int was_touch=0;
 while(1){
  SceCtrlData pad;memset(&pad,0,sizeof(pad));sceCtrlPeekBufferPositive(0,&pad,1);
  unsigned pressed=pad.buttons&~old;old=pad.buttons;
  if(step==SAVING){
   poll_save();
   was_touch=1;
  }else if(!keyboard){
   if(step==0){
    if((pressed&SCE_CTRL_DOWN)&&selected<count-1)selected++;
    if((pressed&SCE_CTRL_UP)&&selected>0)selected--;
    if(pressed&SCE_CTRL_CROSS)prepare();
    if(pressed&SCE_CTRL_TRIANGLE)scan();
    if(pressed&SCE_CTRL_CIRCLE){
     if(!strcmp(browse_dir,SOURCE))break;
     go_back();
    }
   }else if(step==SAVED){
    if(pressed&(SCE_CTRL_CIRCLE|SCE_CTRL_CROSS))finish_review();
   }else{
    if(pressed&SCE_CTRL_CIRCLE)finish_review();
    else if(pressed&SCE_CTRL_CROSS)start_save();
    else if(pressed&SCE_CTRL_TRIANGLE)rename_folder();
    else if(pressed&SCE_CTRL_LTRIGGER)adjust_number(-1);
    else if(pressed&SCE_CTRL_RTRIGGER)adjust_number(1);
   }
   SceTouchData t;memset(&t,0,sizeof(t));
   sceTouchPeek(SCE_TOUCH_PORT_FRONT,&t,1);
   if(t.reportNum&&!was_touch&&!pressed){
    int dx=touch_info.maxDispX-touch_info.minDispX;
    int dy=touch_info.maxDispY-touch_info.minDispY;
    int x=dx>0?(t.report[0].x-touch_info.minDispX)*960/dx:t.report[0].x/2;
    int y=dy>0?(t.report[0].y-touch_info.minDispY)*544/dy:t.report[0].y/2;
    touch_action(x,y);
   }
   was_touch=t.reportNum>0;
  }else{poll_keyboard();was_touch=1;}
  update_animation();
  vita2d_start_drawing();vita2d_clear_screen();draw_backdrop();
  if(step)draw_review();else draw_browser();
  vita2d_end_drawing();
  if(keyboard)vita2d_common_dialog_update();
  vita2d_swap_buffers();
 }
 if(keyboard)sceImeDialogTerm();
 reset_preview();
 if(background)vita2d_free_texture(background);
 if(brand_icon)vita2d_free_texture(brand_icon);
 vita2d_free_pgf(font);vita2d_fini();
 sceSysmoduleUnloadModule(SCE_SYSMODULE_IME);
 sceKernelExitProcess(0);return 0;
}
