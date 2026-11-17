#include "artwork.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

static const unsigned char jpg[]={0xff,0xd8,0xff,0xd9};
static const unsigned char png[]={137,80,78,71,13,10,26,10};
static void size32(unsigned char *d,unsigned v,int safe){
 d[0]=(v>>24)&255;d[1]=(v>>16)&255;d[2]=(v>>8)&255;d[3]=v&255;
 if(safe){d[0]&=127;d[1]&=127;d[2]&=127;d[3]&=127;
   d[0]=(v>>21)&127;d[1]=(v>>14)&127;d[2]=(v>>7)&127;d[3]=v&127;}
}
static void fixture(int ver,int ispng){
 unsigned char payload[64]={0};size_t p=0;
 payload[p++]=0; /* Latin-1 description */
 if(ver==2){
  const char *fmt=ispng?"PNG":"JPG";memcpy(payload+p,fmt,3);p+=3;
 }else{
  const char *mime=ispng?"image/png":"image/jpeg";
  size_t n=strlen(mime)+1;memcpy(payload+p,mime,n);p+=n;
 }
 payload[p++]=3;payload[p++]=0; /* front cover, empty description */
 const unsigned char *img=ispng?png:jpg;size_t n=ispng?sizeof(png):sizeof(jpg);
 memcpy(payload+p,img,n);p+=n;
 unsigned char frame[10]={0};int h=ver==2?6:10;
 memcpy(frame,ver==2?"PIC":"APIC",ver==2?3:4);
 if(ver==2){frame[3]=(p>>16)&255;frame[4]=(p>>8)&255;frame[5]=p&255;}
 else size32(frame+4,(unsigned)p,ver==4);
 unsigned char id3[10]={'I','D','3',(unsigned char)ver,0,0,0,0,0,0};
 size32(id3+6,(unsigned)(h+p),1);
 FILE *f=fopen("/tmp/vcc_fixture.mp3","wb");assert(f);
 assert(fwrite(id3,1,10,f)==10);
 assert(fwrite(frame,1,h,f)==(size_t)h);
 assert(fwrite(payload,1,p,f)==p);
 assert(fclose(f)==0);
 int got_png=-1;
 assert(vcc_extract_artwork("/tmp/vcc_fixture.mp3","/tmp/vcc_cover.tmp",&got_png)==0);
 assert(got_png==ispng);
 unsigned char actual[16]={0};f=fopen("/tmp/vcc_cover.tmp","rb");assert(f);
 assert(fread(actual,1,n,f)==n);assert(fclose(f)==0);
 assert(memcmp(actual,img,n)==0);
 remove("/tmp/vcc_fixture.mp3");remove("/tmp/vcc_cover.tmp");
}
int main(void){
 for(int v=2;v<=4;v++){fixture(v,0);fixture(v,1);}
 FILE *f=fopen("/tmp/vcc_fixture.mp3","wb");assert(f);
 fputs("This MP3 has no ID3 tag",f);fclose(f);
 int fmt=0;
 assert(vcc_extract_artwork("/tmp/vcc_fixture.mp3","/tmp/vcc_cover.tmp",&fmt)==-2);
 remove("/tmp/vcc_fixture.mp3");
 puts("ID3v2.2/v2.3/v2.4 JPEG and PNG extraction + missing artwork: PASS");
 return 0;
}
