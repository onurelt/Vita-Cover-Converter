#pragma once
#include <stddef.h>
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
  size_t n=0;
  for(const unsigned char *p=(const unsigned char*)name;*p;p++,n++){
    if(n>=48)return 0;
    if(!((*p>='A'&&*p<='Z')||(*p>='a'&&*p<='z')||
         (*p>='0'&&*p<='9')||*p=='-'||*p=='_'||*p==' '))return 0;
  }
  return n>0 && name[n-1]!=' ' && name[0]!=' ';
}
