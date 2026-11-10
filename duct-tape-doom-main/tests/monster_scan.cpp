#include <cstddef>
#include "../platform/monster_scan.h"
#include <cstdio>
int main(){
 doomscan::Discovery<32> s;s.sync(1,0,0);
 s.hit(4,false);if(s.update(0,false)!=-1||s.seen[4])return 1;
 s.hit(4,true);s.hit(4,true);s.hit(7,true);
 if(s.pending.size()!=2||s.update(1,true)!=4||s.update(126,true)!=4||s.update(127,true)!=7)return 2;
 s.hit(4,true);if(!s.pending.empty())return 3;
 s.update(128,false);if(s.current!=-1||!s.pending.empty())return 4;
 s.sync(2,0,0);s.hit(4,true);if(s.update(0,true)!=4)return 5;
 s.sync(2,1,1);if(s.current!=-1||s.seen[4])return 6;
 s.hit(5,true);s.sync(2,1,0);if(s.seen[5])return 7;
 s.hit(-1,true);s.hit(32,true);if(!s.pending.empty())return 8;
 puts("PASS monster discovery: default-off, dedup, queue, expiry, toggle and level/viewer reset");return 0;
}
