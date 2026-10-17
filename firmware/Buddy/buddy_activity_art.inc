// Props use a finer two-pixel grid than Buddy's body. Paint only the artwork,
// leaving Buddy visible around each silhouette; retain white interior details.
static void drawPixelProp(bool sleeping, BuddyMoment moment = visibleMoment) {
  if (sleeping || moment == MOMENT_WATCH ||
      moment == MOMENT_MEDITATE || moment == MOMENT_NAP) return;
  if (moment == MOMENT_STARGAZE) {
    spriteOval(178,51,12,13); spriteOval(184,45,11,12,DRIVER_COLOR_WHITE);
    buddyFill(153,69,5,17); buddyFill(147,75,17,5);
    buddyFill(189,82,4,14); buddyFill(184,87,14,4); buddyFill(174,91,4,4);
    return;
  }
  const int x = 150, y = 82, u = 2;
  auto p = [&](int a,int b,int w,int h,uint8_t c=DRIVER_COLOR_BLACK) {
    buddyFill(x+a*u,y+b*u,w*u,h*u,c);
  };
  auto line = [&](int a,int b,int c,int d) {
    spriteLine(x+a*u+1,y+b*u+1,x+c*u+1,y+d*u+1,2);
  };
  auto box = [&](int a,int b,int w,int h) {
    p(a,b,w,h); if (w>2 && h>2) p(a+1,b+1,w-2,h-2,DRIVER_COLOR_WHITE);
  };
  auto oval = [&](int a,int b,int rx,int ry,bool hollow=false) {
    for(int yy=-ry;yy<=ry;yy++) for(int xx=-rx;xx<=rx;xx++) {
      if(xx*xx*ry*ry+yy*yy*rx*rx>rx*rx*ry*ry) continue;
      bool inner=hollow && rx>1 && ry>1 &&
        xx*xx*(ry-1)*(ry-1)+yy*yy*(rx-1)*(rx-1)<=(rx-1)*(rx-1)*(ry-1)*(ry-1);
      p(a+xx,b+yy,1,1,inner?DRIVER_COLOR_WHITE:DRIVER_COLOR_BLACK);
    }
  };
  auto star = [&](int a,int b) { p(a-2,b,5,1); p(a,b-2,1,5); };
  auto note = [&](int a,int b) {
    oval(a,b,2,1); p(a+1,b-8,1,8); p(a+2,b-8,3,2);
  };
  auto item = [&](uint8_t kind,int a=0,int b=3,int scale=3) {
    drawItemArt(kind,x+a*u,y+b*u,scale,buddyItemStyle(buddy.seed,buddy.generation,kind));
  };
  switch(moment) {
    case MOMENT_PLANT: item(OBJECT_PLANT); break;
    case MOMENT_TOY: item(OBJECT_TOY); break;
    case MOMENT_STONE: item(OBJECT_STONE); break;
    case MOMENT_NOOK: item(OBJECT_NOOK); break;
    case MOMENT_NATURE:
      item(OBJECT_GROVE,0,5); line(3,3,5,4); line(5,4,7,3); break;
    case MOMENT_SMELL_FLOWERS:
      item(OBJECT_FLOWERS,6,10,2);
      line(1,12,3,11); line(3,11,4,9); line(2,16,5,14); break;
    case MOMENT_WATER:
      item(OBJECT_PLANT,8,12,2);
      box(1,9,8,6); oval(2,10,2,3,true); p(8,11,2,2);
      line(9,11,12,8); p(12,7,2,2); p(13,11,1,2); p(15,13,1,2); break;
    case MOMENT_GARDEN:
      item(OBJECT_PLANT,8,12,2);
      p(2,6,2,10); p(0,4,6,2); p(1,16,4,5); p(2,21,2,2);
      p(7,26,16,1); p(8,24,2,2); break;
    case MOMENT_POLISH:
      item(OBJECT_STONE,7,11,2);
      p(1,17,5,6); p(2,18,3,2,DRIVER_COLOR_WHITE); star(8,8); break;
    case MOMENT_MEND:
      item(OBJECT_TOY,8,12,2);
      line(1,19,7,10); p(6,11,1,2,DRIVER_COLOR_WHITE);
      line(2,19,1,23); line(1,23,5,25); line(5,25,7,22); break;
    case MOMENT_TRICK:
      item(OBJECT_TOY,7,12,2);
      line(1,17,2,11); line(2,11,6,7); line(6,7,11,6); star(14,5); break;
    case MOMENT_JOURNAL: case MOMENT_STORY:
      item(OBJECT_JOURNAL,7,11,2);
      if(moment==MOMENT_JOURNAL) {
        line(1,20,7,12); line(2,21,8,13); p(1,21,1,2);
      } else { star(4,10); star(15,5); }
      break;
    case MOMENT_MUSIC:
      item(OBJECT_INSTRUMENT,8,12,2); note(3,12); note(14,7); break;
    case MOMENT_HUM: case MOMENT_SING:
      note(5,19); note(16,13);
      if(moment==MOMENT_SING) note(18,26); break;
    case MOMENT_STRETCH:
      line(3,18,3,9); line(3,9,1,12); line(3,9,5,12);
      line(17,18,17,9); line(17,9,15,12); line(17,9,19,12); break;
    case MOMENT_WANDER: case MOMENT_PATROL:
      oval(4,23,2,3); oval(11,17,2,3); oval(17,23,2,3);
      p(3,19,2,1); p(10,13,2,1); p(16,19,2,1);
      if(moment==MOMENT_PATROL) { p(16,4,1,10); p(12,4,10,3); p(21,5,2,1); }
      break;
    case MOMENT_LISTEN:
      // Sound waves, rather than an unrelated window or equalizer.
      line(3,12,5,14); line(5,14,5,18); line(5,18,3,20);
      line(7,9,10,12); line(10,12,10,20); line(10,20,7,23);
      line(12,6,16,10); line(16,10,16,22); line(16,22,12,26); break;
    case MOMENT_DAYDREAM: case MOMENT_VISIT:
      oval(12,11,8,6,true); p(4,20,2,2); p(1,24,1,1);
      if(moment==MOMENT_VISIT) {
        p(8,8,3,3); p(13,8,3,3); p(9,11,6,2); p(10,13,4,1); p(11,14,2,1);
      } else star(12,11);
      break;
    case MOMENT_GROOM:
      oval(16,12,5,7,true); line(16,19,16,26);
      p(3,8,3,14); for(int i=0;i<5;i++) p(6,9+i*2,3,1); break;
    case MOMENT_DANCE:
      note(16,12); line(2,22,6,19); line(6,19,10,22);
      line(3,26,7,23); line(7,23,11,26); break;
    case MOMENT_WARM:
      box(4,14,12,11); oval(17,18,3,3,true); p(5,24,10,2);
      line(6,11,8,8); line(8,8,6,5); line(12,11,14,8); line(14,8,12,5); break;
    case MOMENT_COOL:
      oval(12,12,8,8,true); oval(12,12,2,2);
      p(11,6,3,5); p(14,11,4,3); p(10,14,3,4); p(6,10,5,3);
      p(11,21,2,5); p(7,26,10,2); break;
    case MOMENT_TIDY:
      line(4,5,12,19); line(5,5,13,19);
      p(10,18,9,3); p(9,21,11,5); p(10,26,9,1);
      p(12,22,1,4,DRIVER_COLOR_WHITE); p(16,22,1,4,DRIVER_COLOR_WHITE);
      p(1,26,2,1); p(4,25,2,2); break;
    case MOMENT_BUILD:
      box(4,16,15,6); p(7,11,5,5); p(14,8,2,8); p(16,8,4,2);
      oval(7,24,3,3,true); oval(17,24,3,3,true); p(10,17,5,2); break;
    case MOMENT_EXERCISE:
      p(3,15,18,2); p(2,11,3,10); p(5,9,3,14);
      p(16,9,3,14); p(19,11,3,10); break;
    case MOMENT_STUDY:
      item(OBJECT_JOURNAL,4,13,2);
      oval(15,7,5,5,true); line(19,11,22,14); break;
    case MOMENT_SCULPT:
      p(5,25,16,2); p(8,20,10,5); oval(13,15,5,5); oval(13,7,4,4);
      p(12,7,1,1,DRIVER_COLOR_WHITE); p(15,7,1,1,DRIVER_COLOR_WHITE);
      line(1,17,7,12); p(0,19,2,5); star(20,9); break;
    case MOMENT_FORT:
      for(int row=0;row<18;row++) {
        int half=row/2; p(12-half,7+row,half*2+1,1);
      }
      for(int row=0;row<9;row++) p(12-row/3,16+row,row/3*2+1,1,DRIVER_COLOR_WHITE);
      p(2,25,21,2); break;
    case MOMENT_SORT:
      box(1,19,10,8); box(13,19,10,8);
      oval(5,16,3,2); oval(18,15,3,3,true); p(3,22,6,1); p(15,22,6,1); break;
    case MOMENT_SKETCH:
      line(5,27,12,3); line(12,3,20,27); box(4,7,16,15);
      p(11,15,2,5); p(8,12,8,3); p(10,10,4,2); p(6,22,13,1); break;
    case MOMENT_MAP:
      box(1,9,22,17); p(8,10,1,15); p(15,10,1,15);
      line(4,22,6,18); line(6,18,12,19); line(12,19,19,13);
      line(18,11,21,14); line(21,11,18,14); break;
    case MOMENT_COLLECT:
      oval(12,14,7,8,true); p(4,16,17,10); p(5,17,15,7,DRIVER_COLOR_WHITE);
      p(5,21,15,1); p(8,17,1,7); p(15,17,1,7);
      oval(8,14,3,2); p(14,11,5,5); break;
    case MOMENT_DECORATE:
      line(1,7,12,10); line(12,10,22,7);
      for(int i=0;i<3;i++) for(int row=0;row<5;row++) p(3+i*7+row/2,9+row,5-row,1);
      break;
    case MOMENT_REPAIR_NOOK:
      item(OBJECT_NOOK,7,12,2); line(2,18,8,9); p(4,7,8,3); break;
    case MOMENT_BALANCE:
      p(1,21,22,2); p(4,23,2,5); p(18,23,2,5);
      oval(8,17,2,3); oval(15,14,2,3); break;
    case MOMENT_CELEBRATE:
      for(int row=0;row<12;row++) p(12-row/2,9+row,row+1,1);
      p(5,21,15,2); p(8,17,2,2,DRIVER_COLOR_WHITE); p(12,12,2,2,DRIVER_COLOR_WHITE);
      star(3,9); star(21,17); p(18,4,2,2); p(1,22,2,2); break;
    default: break;
  }
}
