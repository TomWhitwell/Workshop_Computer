// Copyright (c) 2026 Adrian Vos (soveda). SPDX-License-Identifier: MIT
#include "midi_tx.h"
#include <cassert>
#include <cstdio>
#include <deque>
int main(){
 spatial::MidiTx q;std::deque<uint8_t> expected;uint8_t frame[31];unsigned counter=0;
 for(int i=0;i<5000;++i){
  for(auto& b:frame)b=counter++&127;
  bool accepted=q.Enqueue(frame,sizeof(frame));
  if(expected.size()+sizeof(frame)<=256){assert(accepted);for(auto b:frame)expected.push_back(b);}else assert(!accepted);
  size_t n;auto* data=q.Front(n);assert(n<=64&&n<=expected.size());
  // Simulate zero-byte backpressure and partial writes across ring wrap.
  size_t written=i%7==0?0:(n<13?n:13);
  for(size_t j=0;j<written;++j){assert(data[j]==expected.front());expected.pop_front();}
  q.Consume(written);
 }
 while(!expected.empty()){size_t n;auto* data=q.Front(n);assert(n);for(size_t i=0;i<n;++i){assert(data[i]==expected.front());expected.pop_front();}q.Consume(n);}
 assert(q.Empty());q.Enqueue(frame,31);q.Clear();assert(q.Empty());
 std::puts("PASS: nonblocking MIDI TX backpressure, partial writes, wrap, transactional overflow and disconnect clear");
}
