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
 assert(vcc_valid_folder("Track00001"));
 assert(vcc_valid_folder("Track99999"));
 assert(!vcc_valid_folder("Track00000"));
 assert(!vcc_valid_folder("Track0041"));
 assert(!vcc_valid_folder("Track100000"));
 assert(!vcc_valid_folder("Track00A01"));
 assert(!vcc_valid_folder("Dune Soundtrack"));
 assert(!vcc_valid_folder("../escape"));
 puts("folder numbering and path validation: PASS");
 return 0;
}
