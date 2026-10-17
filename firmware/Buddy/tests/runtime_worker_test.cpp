#include <cassert>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include "../buddy_worker_join.h"
#include "../../../third_party/waveshare_epaper_display/bounded_wait.h"
#include "../../../third_party/waveshare_epaper_display/epaper_frame.h"
int main() {
  // Every worker acknowledgement interleaving, including failed startup,
  // acknowledgement at the timeout boundary and a permanently stuck worker.
  for(unsigned ack=0;ack<=61;++ack) {
    unsigned waits=0,deletes=0;bool ready=ack!=0;
    bool forced=buddyJoinOwnedWorker(7,[&]{return ready;},[&]{
      if(++waits==ack)ready=false;
    },[&](int handle){assert(handle==7);++deletes;});
    assert(deletes==1 && waits<=60);
    assert(forced==(ack>60));
  }
  bool deleted=false;
  assert(!buddyJoinOwnedWorker(0,[]{return true;},[]{assert(false);},[&](int){deleted=true;}));
  assert(!deleted);
  for(uint32_t origin: {0U,UINT32_MAX-2000U}) {
    uint32_t tick=origin;
    assert(!epaperWaitIdle([]{return true;},[&]{return tick;},[&]{tick+=5;},5000));
    assert((uint32_t)(tick-origin)==5000);
    tick=origin;
    assert(epaperWaitIdle([&]{return (uint32_t)(tick-origin)<100;},[&]{return tick;},[&]{tick+=5;},5000));
    assert((uint32_t)(tick-origin)==100);
  }
  size_t stride=0;
  assert(epaperFrameLayout(200,200,5000,&stride) && stride==25);
  assert(!epaperFrameLayout(201,200,5000));
  assert(!epaperFrameLayout(200,200,4999));
  assert(!epaperFrameLayout(INT_MAX,INT_MAX,INT_MAX));
  uint8_t frame[4]={0xff,0xff,0xff,0xff};
  assert(epaperWritePixel(frame,sizeof(frame),2,16,2,0,0,true));
  assert(frame[0]==0x7f && frame[1]==0xff);
  assert(epaperWritePixel(frame,sizeof(frame),2,16,2,7,0,true));
  assert(frame[0]==0x7e);
  assert(epaperWritePixel(frame,sizeof(frame),2,16,2,8,0,true));
  assert(frame[1]==0x7f);
  assert(epaperWritePixel(frame,sizeof(frame),2,16,2,15,1,true));
  assert(frame[3]==0xfe);
  assert(epaperWritePixel(frame,sizeof(frame),2,16,2,7,0,false));
  assert(frame[0]==0x7f);
  assert(!epaperWritePixel(frame,sizeof(frame),2,16,2,-1,0,true));
  assert(!epaperWritePixel(frame,sizeof(frame),2,16,2,16,0,true));
  assert(!epaperWritePixel(frame,1,2,16,2,8,0,true));
  assert(!epaperWritePixel(frame,sizeof(frame),0,7,2,0,0,true));
  puts("PASS: owner-only worker deletion interleavings, bounded BUSY and timer rollover");
}
