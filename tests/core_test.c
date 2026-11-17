#include <assert.h>
#include <stdio.h>
#include "core.h"
int main(void){
 assert(vcc_track_number("Track0001")==1);
 assert(vcc_track_number("Track0040")==40);
 assert(vcc_track_number("Track999999")==999999);
 assert(vcc_track_number("Track0000")==-1);
 assert(vcc_track_number("Track0032.old")==-1);
 assert(vcc_track_number("Track0a40")==-1);
 assert(vcc_track_number("TRACK0001")==-1);
 assert(vcc_valid_folder("Track0041"));
 assert(vcc_valid_folder("Dune Soundtrack"));
 assert(vcc_valid_folder("../escape")==0);
 assert(vcc_valid_folder("name/subdir")==0);
 assert(vcc_valid_folder("bad:colon")==0);
 assert(vcc_valid_folder(" trailing ")==0);
 puts("folder numbering and path validation: PASS");
 return 0;
}
