#pragma once
#include <array>
#include <cstddef>
#include <deque>
// Presentation-only discovery state. No actor pointers or game RNG are retained.
namespace doomscan {
template<std::size_t Types> struct Discovery {
 std::array<bool,Types> seen{};
 std::deque<int> pending;
 unsigned level=~0u;int viewer=-1,last=-1,current=-1,start=0;
 void sync(unsigned map,int player,int tic){
  if(map!=level||player!=viewer||tic<last){seen.fill(false);pending.clear();current=-1;level=map;viewer=player;}
  last=tic;
 }
 void hit(int type,bool enabled){if(enabled&&type>=0&&size_t(type)<Types&&!seen[type]){seen[type]=true;pending.push_back(type);}}
 int update(int tic,bool enabled){
  if(!enabled){pending.clear();current=-1;return -1;}
  if(current>=0&&tic-start>=126)current=-1;
  if(current<0&&!pending.empty()){current=pending.front();pending.pop_front();start=tic;}
  return current;
 }
};
}
