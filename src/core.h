#pragma once
#include <stddef.h>
#include <string.h>
/* Pure, host-testable folder naming rules. */
static int vcc_track_number(const char *name){
  if(name[0]!='T'||name[1]!='r'||name[2]!='a'||name[3]!='c'||name[4]!='k'||!name[5])return -1;
  int n=0;
  for(const char *p=name+5;*p;p++){
    if(*p<'0'||*p>'9'||n>99999)return -1;
    n=n*10+(*p-'0');
  }
  return n>0&&n<=999999?n:-1;
}
static int vcc_valid_folder(const char *name){
  if(!name || strlen(name)!=10 || memcmp(name,"Track",5))return 0;
  int number=0;
  for(int i=5;i<10;i++){
    if(name[i]<'0'||name[i]>'9')return 0;
    number=number*10+(name[i]-'0');
  }
  return number>0;
}
