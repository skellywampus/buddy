const uint8_t *buddyGlyph(char c) {
  static const uint8_t space[] = {0,0,0,0,0};
  static const uint8_t glyphs[][5] = {
    {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
    {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
    {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},{0x00,0x41,0x7F,0x41,0x00},
    {0x20,0x40,0x41,0x3F,0x01},{0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
    {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
    {0x7F,0x09,0x09,0x09,0x06},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
    {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},{0x3F,0x40,0x40,0x40,0x3F},
    {0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},{0x63,0x14,0x08,0x14,0x63},
    {0x07,0x08,0x70,0x08,0x07},{0x61,0x51,0x49,0x45,0x43}
  };
  static const uint8_t nums[][5] = {
    {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},{0x62,0x51,0x49,0x49,0x46},
    {0x22,0x41,0x49,0x49,0x36},{0x18,0x14,0x12,0x7F,0x10},{0x2F,0x49,0x49,0x49,0x31},
    {0x3E,0x49,0x49,0x49,0x32},{0x01,0x71,0x09,0x05,0x03},{0x36,0x49,0x49,0x49,0x36},
    {0x26,0x49,0x49,0x49,0x3E}
  };
  static const uint8_t dash[] = {0x08,0x08,0x08,0x08,0x08};
  static const uint8_t dot[] = {0,0x60,0x60,0,0};
  static const uint8_t colon[] = {0,0x36,0x36,0,0};
  static const uint8_t slash[] = {0x20,0x10,0x08,0x04,0x02};
  static const uint8_t percent[] = {0x63,0x13,0x08,0x64,0x63};
  if (c >= 'a' && c <= 'z') c -= 32;
  if (c >= 'A' && c <= 'Z') return glyphs[c - 'A'];
  if (c >= '0' && c <= '9') return nums[c - '0'];
  if (c == '-') return dash;
  if (c == '.') return dot;
  if (c == ':') return colon;
  if (c == '/') return slash;
  if (c == '%') return percent;
  return space;
}

static bool buddySpriteTransformActive = false;
static float buddySpriteScale = 1.0f;
static float buddySpriteOffsetX = 0.0f, buddySpriteOffsetY = 0.0f;

static int buddySpriteMapX(int x) { return (int)lroundf(x * buddySpriteScale + buddySpriteOffsetX); }
static int buddySpriteMapY(int y) { return (int)lroundf(y * buddySpriteScale + buddySpriteOffsetY); }

static void buddyPixelRaw(int x, int y, uint8_t color) {
  if (portraitPixels && x >= 0 && x < 200 && y >= 0 && y < 200) {
    uint8_t mask = 0x80 >> (x % 8);
    if (color == DRIVER_COLOR_BLACK) portraitPixels[y*25+x/8] |= mask;
    else portraitPixels[y*25+x/8] &= ~mask;
    return;
  }
  if (displayRotation180) { x = 199 - x; y = 199 - y; }
  if (display && x >= 0 && x < 200 && y >= 0 && y < 200) display->EPD_DrawColorPixel(x, y, color);
}

void buddyPixel(int x, int y, uint8_t color = DRIVER_COLOR_BLACK) {
  if (buddySpriteTransformActive) { x = buddySpriteMapX(x); y = buddySpriteMapY(y); }
  buddyPixelRaw(x, y, color);
}

void buddyFill(int x, int y, int w, int h, uint8_t color = DRIVER_COLOR_BLACK) {
  if (buddySpriteTransformActive) {
    x = buddySpriteMapX(x); y = buddySpriteMapY(y);
    w = max(1, (int)lroundf(w * buddySpriteScale));
    h = max(1, (int)lroundf(h * buddySpriteScale));
  }
  for (int yy = y; yy < y + h; yy++)
    for (int xx = x; xx < x + w; xx++) buddyPixelRaw(xx, yy, color);
}

void buddyLine(int x0, int y0, int x1, int y1) {
  int dx = abs(x1-x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1-y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;
  while (true) {
    buddyPixel(x0,y0);
    if (x0 == x1 && y0 == y1) break;
    int e2 = 2*err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}

void buddyRect(int x, int y, int w, int h) {
  buddyLine(x,y,x+w-1,y); buddyLine(x,y+h-1,x+w-1,y+h-1);
  buddyLine(x,y,x,y+h-1); buddyLine(x+w-1,y,x+w-1,y+h-1);
}

void buddyChar(int x, int y, char c, uint8_t scale = 1, bool invert = false) {
  const uint8_t *glyph = buddyGlyph(c);
  for (uint8_t col=0; col<5; col++) for (uint8_t row=0; row<7; row++) {
    bool on = glyph[col] & (1 << row);
    if (on || invert) buddyFill(x+col*scale, y+row*scale, scale, scale,
                                (on ^ invert) ? DRIVER_COLOR_BLACK : DRIVER_COLOR_WHITE);
  }
}

void buddyText(int x, int y, const char *text, uint8_t scale = 1, bool invert = false) {
  while (*text) { buddyChar(x,y,*text++,scale,invert); x += 6*scale; }
}

int buddyTextWidth(const char *text, uint8_t scale = 1) { return strlen(text) * 6 * scale; }
void buddyCentered(int y, const char *text, uint8_t scale = 1, bool invert = false) {
  buddyText((200-buddyTextWidth(text,scale))/2,y,text,scale,invert);
}

void buddyHeader(const char *title) {
  buddyText(6,5,title,strlen(title) > 9 ? 1 : 2);
  char battery[12];
  char level[8]; buddyBatteryText(level,sizeof(level),latestBatteryV);
  if (usbHostPowerPresent || batteryChargeTrend) {
    snprintf(battery,sizeof(battery),"PWR %s",level);
  } else {
    snprintf(battery,sizeof(battery),"%s",level);
  }
  buddyText(194-buddyTextWidth(battery),8,battery,1);
  buddyLine(4,27,195,27);
}

BuddyVisualGenome visualGenome() {
  uint32_t mixed = buddy.seed ^ ((uint32_t)buddy.generation * 0x9E3779B9UL);
  BuddyVisualGenome visual = {
    (uint8_t)((mixed >> 1) % 4), (uint8_t)((mixed >> 4) % 6),
    (uint8_t)((mixed >> 8) % 5), (uint8_t)((mixed >> 12) % 6),
    (uint8_t)((mixed >> 16) % 5), (uint8_t)((mixed >> 20) % 4),
    (uint8_t)((mixed >> 24) % 4)
  };
  // Personality leaves a visible family resemblance without making clones.
  if (buddy.traits.curiosity > 75) visual.eyeStyle = 1;
  visual.earStyle = buddy.familyEar;
  visual.markingStyle = buddy.familyMarking;
  if (buddy.traits.playfulness > 80) visual.tuftStyle = 4;
  // A boy keeps the exact procedural portrait he had before the welcome
  // release. Only new girl portraits use a distinct head-shape family.
  if (buddySexFromSeed(buddy.seed) == BUDDY_GIRL)
    visual.headShape |= 1;
  return visual;
}

BuddyVisualGenome nearbyVisualGenome() {
  BuddyVisualGenome visual = visualGenome();
  // The existing v1 BLE recipe has no spare sex bit. Its head-shape parity
  // remains the wire signal, while the local boy portrait stays unchanged.
  visual.headShape = (visual.headShape & 2) | buddySexFromSeed(buddy.seed);
  return visual;
}

static void spriteLine(int x0, int y0, int x1, int y1, int size = 4,
                       uint8_t color = DRIVER_COLOR_BLACK) {
  int dx = x1 - x0, dy = y1 - y0;
  int steps = max(abs(dx), abs(dy)) / max(1, size / 2);
  if (!steps) steps = 1;
  for (int i = 0; i <= steps; i++) {
    int x = x0 + dx * i / steps;
    int y = y0 + dy * i / steps;
    buddyFill(x - size / 2, y - size / 2, size, size, color);
  }
}

static void spriteOval(int cx, int cy, int rx, int ry, uint8_t color = DRIVER_COLOR_BLACK) {
  const int pixel = 4;
  for (int y = -ry; y <= ry; y += pixel) {
    float ratio = 1.0f - ((float)y * y) / ((float)ry * ry);
    int half = ratio > 0 ? (int)(rx * sqrtf(ratio)) : 0;
    half = (half / pixel) * pixel;
    buddyFill(cx - half, cy + y, half * 2 + pixel, pixel, color);
  }
}

static void spriteOutlinedOval(int cx, int cy, int rx, int ry) {
  spriteOval(cx, cy, rx, ry, DRIVER_COLOR_BLACK);
  if (rx > 7 && ry > 7) spriteOval(cx, cy, rx - 5, ry - 5, DRIVER_COLOR_WHITE);
}

static void spriteTriangle(int tipX, int tipY, int baseX, int baseY, int halfBase,
                           uint8_t color = DRIVER_COLOR_BLACK) {
  int height = max(4, abs(baseY - tipY));
  int direction = baseY >= tipY ? 1 : -1;
  for (int offset = 0; offset <= height; offset += 4) {
    int y = tipY + direction * offset;
    int center = tipX + (baseX - tipX) * offset / height;
    int half = halfBase * offset / height;
    buddyFill(center - half, y, half * 2 + 4, 4, color);
  }
}

static void drawPixelEars(const BuddyVisualGenome &visual, int cx, int cy, int rx, int ry, BuddyStage stage) {
  int growth = stage == STAGE_BABY ? 13 : stage == STAGE_CHILD ? 16 : 20;
  int left = cx - rx + 9, right = cx + rx - 9, top = cy - ry + 8;
  switch (visual.earStyle) {
    case 0:
      spriteTriangle(left - 8, top - growth, left + 2, top + 15, 17);
      spriteTriangle(right + 8, top - growth, right - 2, top + 15, 17);
      spriteTriangle(left - 7, top - growth + 8, left + 2, top + 10, 8, DRIVER_COLOR_WHITE);
      spriteTriangle(right + 7, top - growth + 8, right - 2, top + 10, 8, DRIVER_COLOR_WHITE);
      break;
    case 1:
      spriteOutlinedOval(left - 7, top + 1, growth, growth);
      spriteOutlinedOval(right + 7, top + 1, growth, growth);
      break;
    case 2:
      buddyFill(left - growth - 6, top, growth + 10, 30, DRIVER_COLOR_BLACK);
      buddyFill(right - 4, top, growth + 10, 30, DRIVER_COLOR_BLACK);
      buddyFill(left - growth, top + 5, growth, 19, DRIVER_COLOR_WHITE);
      buddyFill(right + 1, top + 5, growth, 19, DRIVER_COLOR_WHITE);
      buddyFill(left - growth - 6, top + 24, 9, 8, DRIVER_COLOR_BLACK);
      buddyFill(right + growth - 3, top + 24, 9, 8, DRIVER_COLOR_BLACK);
      break;
    case 3:
      spriteLine(cx - 18, top + 8, cx - 29, top - growth, 5);
      spriteLine(cx + 18, top + 8, cx + 29, top - growth, 5);
      buddyFill(cx - 35, top - growth - 6, 11, 11);
      buddyFill(cx + 24, top - growth - 6, 11, 11);
      buddyFill(cx - 31, top - growth - 2, 3, 3, DRIVER_COLOR_WHITE);
      buddyFill(cx + 28, top - growth - 2, 3, 3, DRIVER_COLOR_WHITE);
      break;
    case 4:
      spriteTriangle(left + 3, top - growth + 3, left + 8, top + 13, 11);
      spriteTriangle(right - 3, top - growth + 3, right - 8, top + 13, 11);
      break;
    default:
      spriteTriangle(left - growth - 7, top + 3, left + 5, top + 18, 15);
      spriteTriangle(right + growth + 7, top + 3, right - 5, top + 18, 15);
      buddyFill(left - growth - 2, top + 6, 16, 6, DRIVER_COLOR_WHITE);
      buddyFill(right + growth - 14, top + 6, 16, 6, DRIVER_COLOR_WHITE);
      break;
  }
}

static void drawPixelTail(const BuddyVisualGenome &visual, int cx, int cy, int rx,
                          int direction) {
  int root = cx + direction * (rx - 3);
  switch (visual.tailStyle) {
    case 0:
      spriteLine(root, cy + 4, root + direction * 24, cy - 13, 8);
      spriteLine(root + direction * 24, cy - 13, root + direction * 34, cy - 3, 8);
      break;
    case 1:
      spriteLine(root, cy, root + direction * 26, cy - 25, 7);
      spriteOutlinedOval(root + direction * 31, cy - 30, 10, 10);
      break;
    case 2:
      spriteLine(root, cy + 5, root + direction * 18, cy + 16, 8);
      spriteLine(root + direction * 18, cy + 16, root + direction * 34, cy + 3, 8);
      spriteLine(root + direction * 34, cy + 3, root + direction * 23, cy - 7, 8);
      break;
    default:
      spriteLine(root, cy + 3, root + direction * 25, cy + 11, 10);
      spriteLine(root + direction * 25, cy + 11, root + direction * 35, cy - 8, 10);
      spriteLine(root + direction * 35, cy - 8, root + direction * 24, cy - 20, 10);
      break;
  }
}

static void drawPixelTuft(const BuddyVisualGenome &visual, int cx, int top, BuddyStage stage) {
  if (stage == STAGE_BABY && visual.tuftStyle < 3) return;
  int height = stage == STAGE_CHILD ? 9 : 13;
  switch (visual.tuftStyle) {
    case 0:
      spriteTriangle(cx - 5, top - height, cx - 2, top + 5, 7); break;
    case 1:
      spriteTriangle(cx - 11, top - height, cx - 7, top + 5, 7);
      spriteTriangle(cx + 7, top - height + 2, cx + 7, top + 5, 7); break;
    case 2:
      buddyFill(cx - 14, top - 5, 28, 7);
      buddyFill(cx - 9, top - 10, 18, 6); break;
    case 3:
      buddyFill(cx - 6, top - height, 12, 12);
      buddyFill(cx - 2, top - height - 4, 4, height + 10); break;
    default:
      spriteTriangle(cx - 13, top - height, cx - 8, top + 5, 8);
      spriteTriangle(cx, top - height - 3, cx, top + 5, 8);
      spriteTriangle(cx + 13, top - height, cx + 8, top + 5, 8); break;
  }
}

static void drawPixelGirlFeatures(int cx, int headY, int headRx, int headRy) {
  int top = headY - headRy;
  buddyFill(cx-23,top+8,31,6);
  buddyFill(cx-9,top+13,25,6);
  buddyFill(cx+8,top+18,10,5);
  spriteOutlinedOval(cx-headRx+2,headY+14,12,12);
  spriteOutlinedOval(cx+headRx-2,headY+14,12,12);
}

static void drawPixelMarkings(const BuddyVisualGenome &visual, int cx, int headY, int bodyY) {
  switch (visual.markingStyle) {
    case 0:
      buddyFill(cx - 14, headY - 29, 8, 13); buddyFill(cx + 6, headY - 29, 8, 13);
      buddyFill(cx - 4, headY - 31, 8, 17); break;
    case 1:
      buddyFill(cx - 8, headY - 25, 16, 8); buddyFill(cx - 4, headY - 29, 8, 16); break;
    case 2:
      buddyFill(cx - 31, headY + 6, 9, 9); buddyFill(cx + 22, headY + 12, 8, 8);
      buddyFill(cx + 13, bodyY + 3, 11, 11); break;
    case 3:
      buddyFill(cx - 22, bodyY - 2, 44, 8);
      buddyFill(cx - 14, bodyY + 6, 28, 6); break;
    case 4:
      for (int x = -18; x <= 18; x += 12) buddyFill(cx + x - 3, bodyY + abs(x) / 3, 7, 7);
      break;
    default:
      buddyFill(cx - 8, bodyY - 3, 16, 16);
      buddyFill(cx - 4, bodyY + 1, 8, 8, DRIVER_COLOR_WHITE); break;
  }
}

static void drawPixelEyes(const BuddyVisualGenome &visual, int cx, int y, int spread,
                          BuddyStage stage, bool sleeping, BuddyBehavior behavior,
                          BuddyMoment moment, uint16_t mood, uint8_t curiosity) {
  int lx = cx - spread, rx = cx + spread;
  if (sleeping) {
    buddyFill(lx - 9, y, 18, 5); buddyFill(rx - 9, y, 18, 5);
    buddyFill(lx - 5, y + 5, 10, 4); buddyFill(rx - 5, y + 5, 10, 4);
    return;
  }
  if (moment == MOMENT_MEDITATE) {
    // Quiet focus is a facial expression, not an unrelated object.
    buddyFill(lx - 8, y + 2, 16, 4);
    buddyFill(rx - 8, y + 2, 16, 4);
    return;
  }
  int width = stage == STAGE_BABY ? 14 : 12;
  int height = stage == STAGE_BABY ? 18 : 16;
  if (visual.eyeStyle == 3) height = 8;
  if (visual.eyeStyle == 4) { width += 4; height += 4; }
  buddyFill(lx - width / 2, y - height / 2, width, height);
  buddyFill(rx - width / 2, y - height / 2, width, height);
  int gaze = moment == MOMENT_STARGAZE ? 3 :
    behavior == BEHAVIOR_EXPLORE ? 3 : moment == MOMENT_LISTEN ? -3 : 0;
  int eyeRise = moment == MOMENT_STARGAZE ? 1 : 3;
  buddyFill(lx - width / 2 + 3 + gaze, y - height / 2 + eyeRise, 4, 5, DRIVER_COLOR_WHITE);
  buddyFill(rx - width / 2 + 3 + gaze, y - height / 2 + eyeRise, 4, 5, DRIVER_COLOR_WHITE);
  if (visual.eyeStyle == 2) {
    buddyFill(lx + 1, y + 2, 4, 4, DRIVER_COLOR_WHITE);
    buddyFill(rx + 1, y + 2, 4, 4, DRIVER_COLOR_WHITE);
  }
  if (mood < 420) {
    spriteLine(lx - 9, y - 13, lx + 7, y - 9, 4);
    spriteLine(rx - 7, y - 9, rx + 9, y - 13, 4);
  } else if (curiosity > 70) {
    spriteLine(lx - 9, y - 10, lx + 7, y - 14, 4);
    spriteLine(rx - 7, y - 14, rx + 9, y - 10, 4);
  }
}

static void drawPixelMuzzle(const BuddyVisualGenome &visual, int cx, int y, bool sleeping,
                            BuddyBehavior behavior, uint16_t comfort, uint16_t mood) {
  if (visual.muzzleStyle == 0) buddyFill(cx - 4, y, 8, 5);
  else if (visual.muzzleStyle == 1) {
    buddyFill(cx - 6, y, 12, 4); buddyFill(cx - 3, y + 4, 6, 4);
  } else if (visual.muzzleStyle == 2) {
    buddyFill(cx - 5, y - 2, 10, 10); buddyFill(cx - 2, y + 1, 4, 4, DRIVER_COLOR_WHITE);
  } else {
    buddyFill(cx - 6, y, 12, 4); buddyFill(cx - 2, y + 4, 4, 7);
  }
  int mouthY = y + 11;
  if (sleeping) buddyFill(cx - 10, mouthY, 20, 4);
  else if (comfort < 300 || mood < 350) {
    buddyFill(cx - 9, mouthY + 4, 5, 4); buddyFill(cx - 4, mouthY, 8, 4);
    buddyFill(cx + 4, mouthY + 4, 5, 4);
  } else if (behavior == BEHAVIOR_PLAY || mood >= 650) {
    buddyFill(cx - 11, mouthY, 5, 4); buddyFill(cx - 6, mouthY + 4, 12, 4);
    buddyFill(cx + 6, mouthY, 5, 4);
  } else buddyFill(cx - 9, mouthY, 18, 4);
}

// Every life object shares one visual vocabulary on the home screen and by
// the house. Coordinates below are 16x16 logical pixels, scaled for context.
static void itemBlock(int x, int y, int u, int px, int py, int w, int h,
                      uint8_t color = DRIVER_COLOR_BLACK) {
  buddyFill(x + px*u, y + py*u, w*u, h*u, color);
}

static void drawItemArt(uint8_t kind, int x, int y, int u, uint8_t style) {
  auto p = [&](int px, int py, int w, int h, uint8_t c = DRIVER_COLOR_BLACK) {
    itemBlock(x,y,u,px,py,w,h,c);
  };
  switch (kind) {
    case OBJECT_STONE: // smooth, faceted keepsake pebble (no face-like marks)
      p(6,2,5,2); p(4,4,8,2); p(2,6,12,5); p(3,11,10,3); p(6,14,5,1);
      p(5,5,3,2,DRIVER_COLOR_WHITE); p(4,7,3,2,DRIVER_COLOR_WHITE);
      p(6,12,5,1,DRIVER_COLOR_WHITE);
      break;
    case OBJECT_PLANT: // leafy stems above a visibly tapered flowerpot
      p(4,11,8,1); p(5,12,6,3); p(6,15,4,1);
      p(6,12,4,2,DRIVER_COLOR_WHITE); p(7,5,2,6);
      if (style / 2 == 0) {
        p(3,7,5,2); p(2,6,3,2); p(8,5,5,2); p(11,4,3,2);
      } else {
        p(2,5,5,2); p(3,3,3,2); p(9,7,5,2); p(11,5,3,2);
      }
      p(5,7,2,1,DRIVER_COLOR_WHITE); p(10,6,2,1,DRIVER_COLOR_WHITE);
      if (style & 1) { p(6,3,4,2); p(7,1,2,2); }
      break;
    case OBJECT_NOOK: // sheltered nook with a cushion inside its open arch
      p(7,1,2,2); p(5,3,6,2); p(3,5,10,2); p(1,7,14,2);
      p(2,9,12,6); p(5,9,6,6,DRIVER_COLOR_WHITE);
      p(6,12,4,2); p(5,14,6,1);
      p(0,15,16,1);
      break;
    case OBJECT_TOY:
      if (style / 3 == 0) { // patchwork ball with seams, not face-like dots
        p(5,1,6,1); p(3,2,10,2); p(2,4,12,2); p(1,6,14,6);
        p(2,12,12,2); p(4,14,8,1);
        p(4,4,8,8,DRIVER_COLOR_WHITE); p(6,2,4,2,DRIVER_COLOR_WHITE);
        p(2,6,2,4,DRIVER_COLOR_WHITE); p(12,6,2,4,DRIVER_COLOR_WHITE);
        p(5,5,6,1); p(8,5,1,6); p(5,10,5,1,DRIVER_COLOR_WHITE);
        if (style & 1) p(4,8,3,1);
        if (style & 2) p(9,8,3,1);
      } else if (style / 3 == 1) { // diamond kite with a long, visible tail
        p(7,1,2,2); p(5,3,6,2); p(3,5,10,3); p(5,8,6,2);
        p(7,10,2,2); p(7,12,1,2); p(5,13,2,1); p(9,14,2,1);
        p(7,4,2,5,DRIVER_COLOR_WHITE); p(4,6,8,1,DRIVER_COLOR_WHITE);
        if (style & 1) p(5,5,2,1,DRIVER_COLOR_WHITE);
        if (style & 2) p(9,7,2,1,DRIVER_COLOR_WHITE);
      } else if (style / 3 == 2) { // classic spinning top
        p(7,1,2,3); p(5,4,6,2); p(3,6,10,2); p(2,8,12,3);
        p(4,11,8,2); p(6,13,4,1); p(7,14,2,2);
        p(4,9,8,1,DRIVER_COLOR_WHITE); p(7,6,2,2,DRIVER_COLOR_WHITE);
        if (style & 1) p(5,8,2,1,DRIVER_COLOR_WHITE);
        if (style & 2) p(9,8,2,1,DRIVER_COLOR_WHITE);
      } else { // small sailboat carved from a scrap of wood
        p(3,11,10,2); p(1,13,14,1); p(4,14,8,1);
        p(7,2,2,9); p(4,4,3,5); p(9,3,3,6);
        p(5,5,1,3,DRIVER_COLOR_WHITE); p(10,5,1,3,DRIVER_COLOR_WHITE);
        if(style % 3 == 0) p(3,11,3,1,DRIVER_COLOR_WHITE);
        else if(style % 3 == 1) { p(9,1,4,1); p(11,5,1,2,DRIVER_COLOR_WHITE); }
        else { p(9,6,3,1,DRIVER_COLOR_WHITE); p(9,11,3,1,DRIVER_COLOR_WHITE); }
      }
      break;
    case OBJECT_GROVE: // two separate trees and a shared bit of ground
      p(3,3,5,2); p(2,5,7,3); p(1,8,9,2); p(5,10,2,5);
      p(10,1,3,2); p(9,3,5,3); p(8,6,7,3); p(11,9,2,6);
      p(1,15,14,1);
      p(3,6,3,1,DRIVER_COLOR_WHITE); p(10,4,2,1,DRIVER_COLOR_WHITE);
      break;
    case OBJECT_FLOWERS:
      if (style / 2 == 0) { // single broad blossom
        p(6,1,4,2); p(3,3,10,3); p(6,6,4,3);
        p(7,9,2,6); p(3,11,4,2); p(9,10,4,2);
        p(6,4,4,3,DRIVER_COLOR_WHITE); p(7,5,2,2);
        if (style & 1) p(6,10,2,2);
      } else { // two smaller flowers
        p(3,1,3,2); p(1,3,7,3); p(3,6,3,2); p(4,8,1,6);
        p(10,0,3,2); p(8,2,7,3); p(10,5,3,2); p(11,7,1,7);
        p(4,4,1,1,DRIVER_COLOR_WHITE); p(11,3,1,1,DRIVER_COLOR_WHITE);
        p(2,10,3,2); p(12,9,3,2);
        p(1,14,14,2);
        if (style & 1) { p(2,11,2,1,DRIVER_COLOR_WHITE); p(13,10,2,1,DRIVER_COLOR_WHITE); }
      }
      break;
    case OBJECT_JOURNAL: // opened book, central binding, visible writing
      p(1,3,14,11); p(3,2,5,11); p(8,2,5,11);
      p(3,4,4,7,DRIVER_COLOR_WHITE); p(9,4,4,7,DRIVER_COLOR_WHITE);
      p(4,5,3,1); p(4,8,2,1); p(9,5,3,1); p(9,8,3,1);
      p(7,3,2,11); p(2,14,12,1);
      break;
    case OBJECT_INSTRUMENT: // little drum and two unmistakable sticks
      p(2,5,12,2); p(3,7,10,6); p(4,13,8,2);
      p(4,8,1,4,DRIVER_COLOR_WHITE); p(11,8,1,4,DRIVER_COLOR_WHITE);
      p(5,1,1,4); p(4,1,3,1); p(10,1,1,4); p(9,1,3,1);
      p(4,6,8,1,DRIVER_COLOR_WHITE);
      p(6,8,1,2,DRIVER_COLOR_WHITE); p(7,10,1,2,DRIVER_COLOR_WHITE);
      p(9,8,1,2,DRIVER_COLOR_WHITE); p(8,10,1,2,DRIVER_COLOR_WHITE);
      break;
  }
}

#include "buddy_activity_art.inc"

static void drawBuddyHouse() {
  if (houseLocked || buddyHouse.magic != BUDDY_HOUSE_MAGIC || buddyHouse.phase == HOUSE_EMPTY) return;
  const int x = 7, y = 99;
  buddyFill(x, y + 36, 45, 3); // ground line
  if (buddyHouse.phase == HOUSE_RUIN) {
    buddyFill(x + 3, y + 27, 12, 8);
    buddyFill(x + 17, y + 31, 20, 4);
    buddyFill(x + 33, y + 22, 7, 12);
    buddyFill(x + 8, y + 23, 9, 3);
    return;
  }
  const bool occupied = buddyHouse.phase == HOUSE_OCCUPIED;
  const uint16_t work = buddyHouse.work;
  buddyFill(x + 5, y + 32, 36, 4); // foundation
  if (!occupied && work < 130) return;
  buddyFill(x + 7, y + 13, 4, 20);
  if (!occupied && work < 270) return;
  buddyFill(x + 35, y + 13, 4, 20);
  if (!occupied && work < 410) return;
  buddyFill(x + 11, y + 12, 24, 4);
  if (!occupied && work < 550) return;
  for (int i = 0; i < 5; i++) {
    buddyFill(x + 3 + i * 4, y + 8 - i * 3, 6, 4);
    buddyFill(x + 23 + i * 4, y - 4 + i * 3, 6, 4);
  }
  if (!occupied) return;
  buddyFill(x + 12, y + 16, 22, 15, DRIVER_COLOR_WHITE);
  buddyFill(x + 12, y + 17, 21, 2);
  buddyFill(x + 16, y + 22, 7, 9); // door
  buddyFill(x + 26, y + 22, 5, 5); // window
  buddyFill(x + 27, y + 23, 3, 3, DRIVER_COLOR_WHITE);
  if (buddyHouse.style & 1) buddyFill(x + 32, y - 5, 5, 11); // chimney
  if (buddyHouse.style & 2) buddyFill(x + 3, y + 25, 4, 7); // step
  if (buddyHouse.condition < 650) {
    buddyFill(x + 34, y + 16, 3, 3, DRIVER_COLOR_WHITE);
    buddyFill(x + 35, y + 21, 3, 5, DRIVER_COLOR_WHITE);
  }
  if (buddyHouse.condition < 300) {
    buddyFill(x + 8, y + 8, 6, 4, DRIVER_COLOR_WHITE);
    buddyFill(x + 11, y + 12, 4, 5, DRIVER_COLOR_WHITE);
    buddyFill(x + 38, y + 30, 5, 4);
  }
  // A well-used favorite finds a visible place beside the home. This is
  // derived from the living Buddy's possession and affection, not a new menu.
  uint8_t favorite = buddy.favoriteObject;
  if (favorite < OBJECT_COUNT && (buddy.possessions & (1 << favorite)) &&
      buddy.visits[favorite] >= 5 && buddy.affection[favorite] >= 120) {
    drawItemArt(favorite,x+47,y+20,1,
                buddyItemStyle(buddy.seed,buddy.generation,favorite));
  }
}

static void drawBuddyLegacies() {
  if (legacyLocked || buddyLegacy.magic != BUDDY_LEGACY_MAGIC) return;
  // Show one object-sized work in the open space left of Buddy. A relevant
  // activity takes precedence; otherwise different surviving works get their
  // turn on successive days. No extra screen or exposed numeric inventory.
  uint8_t kind = objectForMoment(visibleMoment);
  if (!buddyLegacyUsable(buddyLegacy, kind)) {
    kind = !ambitionLocked && buddyAmbition.kind < OBJECT_COUNT &&
      buddyLegacyUsable(buddyLegacy, buddyAmbition.kind) ? buddyAmbition.kind : OBJECT_COUNT;
  }
  if (kind == OBJECT_COUNT) {
    uint8_t available[OBJECT_COUNT], count = 0;
    for (uint8_t i = 0; i < OBJECT_COUNT; i++)
      if (buddyLegacyUsable(buddyLegacy, i)) available[count++] = i;
    if (!count) return;
    kind = available[(buddyNow() / 86400UL + buddyHouse.siteSeed) % count];
  }
  const int x = 5, y = 64;
  if (kind == OBJECT_STONE) { // a broad tablet with an engraved route
    buddyFill(x+1,y+3,25,19);
    buddyFill(x+3,y+5,21,15,DRIVER_COLOR_WHITE);
    buddyLine(x+4,y+17,x+9,y+10);
    buddyLine(x+9,y+10,x+14,y+14);
    buddyLine(x+14,y+14,x+22,y+7);
    buddyFill(x+5,y+7,3,3);
    buddyFill(x+19,y+15,3,3);
  } else if (kind == OBJECT_PLANT) { // a lush garden trellis
    buddyFill(x+1,y+20,27,3);
    buddyFill(x+13,y+4,3,16);
    buddyFill(x+4,y+7,11,5);
    buddyFill(x+15,y+11,11,5);
    buddyFill(x+6,y+15,8,5);
    buddyFill(x+12,y+2,5,4);
  } else if (kind == OBJECT_NOOK) { // a lantern welcoming Buddy home
    buddyFill(x+2,y+6,24,3);
    buddyFill(x+6,y+2,16,4);
    buddyFill(x+4,y+9,3,13);
    buddyFill(x+21,y+9,3,13);
    buddyFill(x+7,y+19,14,3);
    buddyFill(x+13,y+10,3,7);
    buddyFill(x+10,y+13,9,3);
  } else if (kind == OBJECT_TOY) { // a broad windwheel made from the toy
    buddyFill(x+13,y+9,3,14);
    buddyFill(x+5,y+8,20,3);
    buddyFill(x+12,y+1,4,18);
    buddyFill(x+4,y+3,7,5);
    buddyFill(x+18,y+14,7,5);
    buddyFill(x+12,y+8,4,4,DRIVER_COLOR_WHITE);
  } else if (kind == OBJECT_GROVE) { // a little trail marker among trees
    buddyFill(x+4,y+7,5,16); buddyFill(x+19,y+5,5,18);
    buddyFill(x+1,y+4,12,5); buddyFill(x+16,y+2,12,5);
    buddyFill(x+11,y+12,6,11); buddyFill(x+12,y+4,4,8,DRIVER_COLOR_WHITE);
  } else if (kind == OBJECT_FLOWERS) { // a planted flower bed
    buddyFill(x+2,y+20,26,3);
    for (int i=0;i<3;i++) {
      int px=x+6+i*8; buddyFill(px,y+10,2,10);
      buddyFill(px-2,y+5,6,6); buddyFill(px,y+7,2,2,DRIVER_COLOR_WHITE);
    }
  } else if (kind == OBJECT_JOURNAL) { // bound family storybook
    buddyFill(x+3,y+3,22,20);
    buddyFill(x+7,y+5,16,16,DRIVER_COLOR_WHITE);
    buddyFill(x+5,y+3,3,20);
    buddyFill(x+10,y+9,10,2); buddyFill(x+10,y+14,8,2);
  } else { // a handmade instrument with sound holes
    buddyFill(x+10,y+3,8,19);
    buddyFill(x+5,y+15,18,7);
    buddyFill(x+8,y+17,3,3,DRIVER_COLOR_WHITE);
    buddyFill(x+17,y+17,3,3,DRIVER_COLOR_WHITE);
    buddyFill(x+14,y+1,10,3);
  }
  if (buddyLegacy.condition[kind] < 28) {
    buddyFill(x+1,y+3,4,4,DRIVER_COLOR_WHITE);
    buddyFill(x+23,y+18,4,4,DRIVER_COLOR_WHITE);
  }
}

static void drawBuddyCharacter(const BuddyVisualGenome &visual, BuddyStage stage,
                               bool girl, BuddyBehavior behavior, BuddyMoment moment,
                               bool sleeping, uint16_t comfort, uint16_t mood,
                               uint8_t curiosity, int tailDirection, bool hot) {
  int headRx[] = {43,42,42,44,43,43};
  int headRy[] = {38,37,37,38,37,37};
  int bodyRx[] = {29,35,39,42,41,41};
  int bodyRy[] = {25,31,35,38,36,36};
  int hrx = headRx[stage], hry = headRy[stage], brx = bodyRx[stage], bry = bodyRy[stage];
  if (girl) { brx -= 3; bry += 2; }
  if (visual.headShape == 1) { hrx += 5; hry -= 3; }
  else if (visual.headShape == 2) { hrx -= 3; hry += 4; }
  else if (visual.headShape == 3) { hrx += 3; hry += 2; }
  int headY = stage == STAGE_BABY ? 55 : 52;
  int bodyY = stage == STAGE_BABY ? 108 : 106;

  drawPixelTail(visual, 100, bodyY, brx, tailDirection);
  drawPixelEars(visual, 100, headY, hrx, hry, stage);
  spriteOutlinedOval(100, bodyY, brx, bry);

  // Chunky feet and arms form distinct sprite poses.
  spriteOutlinedOval(78, bodyY + bry - 2, 17, 9);
  spriteOutlinedOval(122, bodyY + bry - 2, 17, 9);
  if (sleeping || behavior == BEHAVIOR_SLEEP) {
    spriteLine(100 - brx + 6, bodyY - 3, 108, bodyY + 12, 7);
    spriteLine(100 + brx - 6, bodyY - 3, 92, bodyY + 12, 7);
  } else if (behavior == BEHAVIOR_PLAY) {
    spriteLine(100 - brx + 5, bodyY - 7, 100 - brx - 20, bodyY - 30, 7);
    spriteLine(100 + brx - 5, bodyY - 7, 100 + brx + 20, bodyY - 30, 7);
    buddyFill(100 - brx - 25, bodyY - 35, 10, 10);
    buddyFill(100 + brx + 15, bodyY - 35, 10, 10);
  } else if (behavior == BEHAVIOR_EXPLORE) {
    spriteLine(100 - brx + 5, bodyY, 100 - brx - 21, bodyY + 12, 7);
    spriteLine(100 + brx - 5, bodyY, 100 + brx + 23, bodyY - 13, 7);
    buddyFill(100 + brx + 18, bodyY - 18, 10, 10);
  } else if (comfort < 350) {
    spriteLine(100 - brx + 6, bodyY - 3, 111, bodyY + 10, 7);
    spriteLine(100 + brx - 6, bodyY - 3, 89, bodyY + 10, 7);
  } else {
    spriteLine(100 - brx + 5, bodyY - 5, 100 - brx - 12, bodyY + 18, 7);
    spriteLine(100 + brx - 5, bodyY - 5, 100 + brx + 12, bodyY + 18, 7);
    buddyFill(100 - brx - 17, bodyY + 14, 10, 10);
    buddyFill(100 + brx + 7, bodyY + 14, 10, 10);
  }

  spriteOutlinedOval(100, headY, hrx, hry);
  drawPixelTuft(visual, 100, headY - hry, stage);
  drawPixelMarkings(visual, 100, headY, bodyY);
  if (girl) drawPixelGirlFeatures(100,headY,hrx,hry);
  drawPixelEyes(visual, 100, headY - 5, hrx / 2, stage, sleeping,
                behavior, moment, mood, curiosity);
  drawPixelMuzzle(visual, 100, headY + 8, sleeping, behavior, comfort, mood);

  if (stage == STAGE_ELDER) {
    buddyFill(68, headY - 13, 14, 4); buddyFill(118, headY - 13, 14, 4);
    buddyFill(72, headY + 6, 8, 4); buddyFill(120, headY + 6, 8, 4);
  }
  if (!sleeping && hot) {
    buddyFill(147, 39, 6, 13); buddyFill(143, 50, 6, 7); buddyFill(151, 55, 6, 7);
  }
}

void drawBuddyFace(bool sleeping = false) {
  drawBuddyHouse();
  drawBuddyLegacies();
  drawBuddyCharacter(visualGenome(), (BuddyStage)buddy.currentStage,
                     buddySexFromSeed(buddy.seed) == BUDDY_GIRL,
                     visibleBehavior, visibleMoment, sleeping, buddy.comfort,
                     buddy.mood, buddy.traits.curiosity,
                     (buddy.generation & 1) ? 1 : -1,
                     latestClimateValid && latestTempC > 29.0f);
  drawPixelProp(sleeping);
  if (sleeping) { buddyText(159,35,"Z",2); buddyText(179,22,"Z",1); }
}

static void drawCompactEncounterBuddy(int centerX, const BuddyVisualGenome &visual,
                                      BuddyStage stage, uint8_t activity, BuddyMoment moment,
                                      bool girl, uint32_t identity, bool local,
                                      uint8_t expression) {
  // Draw the exact home portrait at a smaller physical size. The source
  // geometry keeps every marking, ear, tail and facial detail of each life.
  buddySpriteScale = 0.64f;
  buddySpriteOffsetX = centerX - 100.0f * buddySpriteScale;
  buddySpriteOffsetY = 31.0f;
  buddySpriteTransformActive = true;
  BuddyBehavior behavior = moment < MOMENT_COUNT ? behaviorForMoment(moment) :
    activity == NEAR_PLAY ? BEHAVIOR_PLAY :
    activity == NEAR_EXPLORE ? BEHAVIOR_EXPLORE : BEHAVIOR_OBSERVE;
  bool sleeping = moment < MOMENT_COUNT ? moment == MOMENT_NAP : activity == NEAR_REST;
  drawBuddyCharacter(visual, stage, girl, behavior,
                     moment < MOMENT_COUNT ? moment : MOMENT_WATCH, sleeping,
                     local ? buddy.comfort : buddyCableComfort(expression),
                     local ? buddy.mood : buddyCableMood(expression),
                     local ? buddy.traits.curiosity : buddyCableCuriosity(expression),
                     (identity & 1) ? 1 : -1, false);
  buddySpriteTransformActive = false;
  buddySpriteScale = 1.0f;
  buddySpriteOffsetX = buddySpriteOffsetY = 0.0f;
}

static void renderCableGreetingView() {
  const NearbyPacket &peer = cableScenePeer;
  drawCompactEncounterBuddy(50, visualGenome(), (BuddyStage)buddy.currentStage,
                            cableSceneActivity, (BuddyMoment)cableSceneMoment,
                            buddySexFromSeed(buddy.seed) == BUDDY_GIRL,
                            buddy.generation, true, 0);
  drawCompactEncounterBuddy(150, peer.visual, (BuddyStage)peer.stage,
                            cableSceneActivity, (BuddyMoment)cableSceneMoment,
                            peer.girl, peer.sender, false, peer.expression);
  buddyCentered(8, "CABLE FRIENDS", 1);
  buddyCentered(148, buddyCableGreetingText(cableGreetingKnown), 2);
  buddyCentered(174, cableGreetingKnown ? "WELCOME BACK" : "HELLO, FRIEND", 1);
}

static void renderNearbyScene(uint32_t now) {
  const NearbyPacket &peer = cableFriendsActive() && cableScenePeer.sender ?
    cableScenePeer : nearbyEncounter.peer;
  uint8_t activity = cableFriendsActive() && cableScenePeer.sender ?
    cableSceneActivity : nearbyEncounter.activity;
  BuddyMoment moment = cableFriendsActive() && cableScenePeer.sender ?
    (BuddyMoment)cableSceneMoment : MOMENT_COUNT;
  drawCompactEncounterBuddy(50, visualGenome(), (BuddyStage)buddy.currentStage,
                            activity, moment,
                            buddySexFromSeed(buddy.seed) == BUDDY_GIRL,
                            buddy.generation, true, 0);
  drawCompactEncounterBuddy(150, peer.visual,
                            (BuddyStage)peer.stage,
                            activity, moment,
                            cableFriendsActive() ? peer.girl : (peer.visual.headShape & 1),
                            peer.sender, false, peer.expression);
  if (moment == MOMENT_STARGAZE) {
    // Both portraits look toward one shared sky above their heads.
    spriteOval(100,18,10,11);
    spriteOval(105,13,9,10,DRIVER_COLOR_WHITE);
    buddyFill(78,15,4,12); buddyFill(74,19,12,4);
    buddyFill(122,16,4,12); buddyFill(118,20,12,4);
  } else if (moment < MOMENT_COUNT) {
    // A shared moment uses the same identifiable artwork as the solo scene,
    // fitted into the clear space between the two complete portraits.
    buddySpriteScale = 0.5f;
    buddySpriteOffsetX = 13.0f;
    buddySpriteOffsetY = 63.0f;
    buddySpriteTransformActive = true;
    drawPixelProp(false,moment);
    buddySpriteTransformActive = false;
    buddySpriteScale = 1.0f;
    buddySpriteOffsetX = buddySpriteOffsetY = 0.0f;
  } else if (activity == NEAR_GARDEN || activity == NEAR_PLAY) {
    uint8_t kind = activity == NEAR_GARDEN ? OBJECT_PLANT : OBJECT_TOY;
    drawItemArt(kind,92,120,1,buddyItemStyle(buddy.seed,buddy.generation,kind));
  }
  buddyCentered(148, moment < MOMENT_COUNT ? buddyMomentText(moment) :
                nearbyActivityText(activity), 1);
  buddyCentered(169, "TOGETHER", 1);
  char footer[36];
  char level[8]; buddyBatteryText(level,sizeof(level),latestBatteryV);
  if (clockFault) snprintf(footer,sizeof(footer),"CLOCK - AGING PAUSED");
  else snprintf(footer,sizeof(footer),"GEN %u  DAY %lu  BAT %s",buddy.generation,
                (unsigned long)buddyAgeDays(now),level);
  buddyCentered(187,footer,1);
}

// E-paper retains this frame through the GPIO/USB restart. On a real meeting
// it keeps the two recognizable portraits visible while Buddy heads home.
void renderCableFarewell(bool met) {
  if (!display) return;
  display->EPD_Clear();
  buddyRect(0,0,200,200);
  if (met && cableScenePeer.sender && validNearbyVisual(cableScenePeer.visual))
    renderNearbyScene(buddyNow());
  else drawBuddyFace(false);
  buddyFill(4,142,192,52,DRIVER_COLOR_WHITE);
  buddyCentered(151,met ? "SEE YOU SOON" : "HEADING HOME",2);
  buddyCentered(177,met ? "UNTIL NEXT TIME" : "BACK TO BUDDY",1);
  display->EPD_Init();
  display->EPD_Display();
  display->EPD_Init_Partial();
  partialReady = true;
  partialCount = 0;
  cleanPartialRequested = false;
}

static void drawMemorialHeadstone() {
  // A quiet, dedicated memorial image. It deliberately stops above the life
  // summary so the e-paper's persistent ink cannot obscure either line.
  buddyFill(42,128,116,4);                 // ground
  buddyFill(57,57,86,71);                  // stone body
  buddyFill(63,45,74,12);                  // stepped arch
  buddyFill(71,39,58,8);
  buddyFill(57,68,6,54,DRIVER_COLOR_WHITE); // outlined sides
  buddyFill(137,68,6,54,DRIVER_COLOR_WHITE);
  buddyFill(63,57,74,65,DRIVER_COLOR_WHITE);
  buddyFill(67,61,66,57);                  // inner face
  buddyFill(71,65,58,49,DRIVER_COLOR_WHITE);
  buddyCentered(69,"BUDDY",2);
  buddyFill(97,87,6,20);                   // simple memorial star
  buddyFill(90,94,20,6);
  buddyFill(94,102,12,4);

  // Two small flowers soften the scene while staying outside the inscription.
  buddyFill(48,113,3,15); buddyFill(42,108,7,7); buddyFill(50,106,7,7);
  buddyFill(45,111,9,3,DRIVER_COLOR_WHITE);
  buddyFill(149,113,3,15); buddyFill(143,106,7,7); buddyFill(151,108,7,7);
  buddyFill(146,111,9,3,DRIVER_COLOR_WHITE);
}

void renderHomeView() {
  if (!buddy.alive) {
    buddyHeader("MEMORIAL");
    drawMemorialHeadstone();
    char line[28];
    snprintf(line,sizeof(line),"LIVED %u DAYS",buddy.prior.livedDays);
    buddyCentered(150,line,1);
    buddyCentered(166,archiveBlocked ? "INSERT SD TO KEEP HISTORY" :
#ifdef BUDDY_ACCELERATED_LIFE_TEST
      "TEST: NEXT LIFE AFTER 2 MIN",1);
#else
      "A NEW LIFE AFTER ONE DAY",1);
#endif
    if (!buddy.memorialShown) {
      buddy.memorialShown = true;
      saveBuddy();
    }
    return;
  }
  uint32_t now = buddyNow();
  if (!renderingDoze && nearbySceneVisible()) {
    if (cableFriendsActive() && cableScenePeer.sender &&
        buddyCableGreetingVisible(cableGreetingAwaitingPaint,
                                 cableGreetingUntilMs, millis()))
      renderCableGreetingView();
    else renderNearbyScene(now);
    return;
  }
  drawBuddyFace(renderingDoze || visibleBehavior == BEHAVIOR_SLEEP);
  bool hearing = !renderingDoze && buddyVoiceUntilMs && (int32_t)(buddyVoiceUntilMs - millis()) > 0;
  bool command = !renderingDoze && buddyVoiceCommandUntilMs &&
    (int32_t)(buddyVoiceCommandUntilMs - millis()) > 0;
  if (renderingDoze) {
    // A solid label separates the doze status from the sprite's feet.
    buddyFill(4,149,192,21);
    buddyCentered(153,"DOZING",2,true);
  } else buddyCentered(153,command ? (buddyVoiceCommandFailed ? "UNAVAILABLE" :
      buddyVoiceCommandText(buddyLastVoiceCommand)) : hearing ? "ATTENTIVE" : visibleMoodName(now),2);
  buddyCentered(173,renderingDoze ? "CHECKS IN HOURLY" : cableFriendsActive() ?
    (cableSession.phase == CABLE_CONNECTED ? "FRIEND CONNECTED" : "WAITING FOR BUDDY") : command ? "VOICE COMMAND" :
    hearing ? "HEARD YOUR VOICE" : buddyMomentText(),1);
  char footer[36];
  char level[8]; buddyBatteryText(level,sizeof(level),latestBatteryV);
#ifdef BUDDY_ACCELERATED_LIFE_TEST
  snprintf(footer,sizeof(footer),"GEN %u DAY %lu BAT %s 6H TEST",buddy.generation,
#else
  snprintf(footer,sizeof(footer),"GEN %u  DAY %lu  BAT %s",buddy.generation,
#endif
           (unsigned long)buddyAgeDays(now),level);
  if (renderingDoze) snprintf(footer,sizeof(footer),"HOLD RESET TO WAKE");
  else if (cableFriendsActive()) snprintf(footer,sizeof(footer),"HOLD BOOT TO EXIT");
  if (clockFault && !cableReturnActivityOverride)
    snprintf(footer,sizeof(footer),"CLOCK - AGING PAUSED");
  else if (saveFailed) snprintf(footer,sizeof(footer),"SAVE RETRY PENDING");
  else if (houseLocked) snprintf(footer,sizeof(footer),"HOME SAVE NEEDS REPAIR");
  else if (houseSaveFailed) snprintf(footer,sizeof(footer),"HOME SAVE RETRY PENDING");
  else if (legacyLocked) snprintf(footer,sizeof(footer),"SITE SAVE NEEDS REPAIR");
  else if (legacySaveFailed) snprintf(footer,sizeof(footer),"SITE SAVE RETRY PENDING");
  if (renderingDoze && sleepSdCardPresent) buddyCentered(178,"SD SAFE TO SWAP",1);
  buddyCentered(187,footer,1);
}

static const char *qualityName(uint16_t value, const char *low, const char *mid, const char *high) {
  return value < 350 ? low : value < 760 ? mid : high;
}

static void topTraitNames(const char *&first, const char *&second) {
  static const char *names[] = {"CURIOUS", "SOCIAL", "PLAYFUL", "RESILIENT", "SENSITIVE", "INDEPENDENT"};
  const uint8_t *traits = (const uint8_t *)&buddy.traits;
  uint8_t a = 0, b = 1;
  if (traits[b] > traits[a]) { uint8_t t=a; a=b; b=t; }
  for (uint8_t i=2; i<6; i++) {
    if (traits[i] > traits[a]) { b=a; a=i; }
    else if (traits[i] > traits[b]) b=i;
  }
  first=names[a]; second=names[b];
}

void renderAboutView() {
  buddyHeader("ABOUT BUDDY");
  char line[40]; const char *first, *second; topTraitNames(first,second);
  snprintf(line,sizeof(line),"%s %s  DAY %lu",stageName((BuddyStage)buddy.currentStage),
           buddySexFromSeed(buddy.seed)==BUDDY_GIRL ? "GIRL" : "BOY",
           (unsigned long)buddyAgeDays(buddyNow()));
  buddyCentered(36,line,1);
  snprintf(line,sizeof(line),"FEELS  %s",visibleMoodName(buddyNow())); buddyCentered(54,line,1);
  snprintf(line,sizeof(line),"ENERGY  %s",qualityName(buddy.energy,"EXHAUSTED","STEADY","LIVELY")); buddyCentered(72,line,1);
  snprintf(line,sizeof(line),"HEALTH  %s",qualityName(buddy.health,"UNWELL","WELL","THRIVING")); buddyCentered(90,line,1);
  snprintf(line,sizeof(line),"NATURE  %s + %s",first,second); buddyCentered(108,line,1);
  snprintf(line,sizeof(line),"LOVES  %s",buddy.favoriteObject < OBJECT_COUNT ? objectName(buddy.favoriteObject) : "EXPLORING"); buddyCentered(126,line,1);
  snprintf(line,sizeof(line),"BOND  %s",qualityName(buddy.bond,"NEW","CLOSE","DEVOTED")); buddyCentered(144,line,1);
  if (latestClimateValid) snprintf(line,sizeof(line),"TEMP  %.1fF - %s",latestTempC*9.0f/5.0f+32.0f,temperatureFeeling(latestTempC));
  else snprintf(line,sizeof(line),"TEMP  UNAVAILABLE");
  buddyCentered(162,line,1);
  buddyFill(4,181,192,16); buddyCentered(187,"CLICK SETTINGS",1,true);
}

bool exportPortrait(const char *path) {
  uint8_t *pixels = (uint8_t *)calloc(5000,1);
  if (!pixels) return false;
  portraitPixels = pixels;
  drawBuddyFace(true);
  portraitPixels = nullptr;
  // Portable ASCII PBM works with standard image viewers/converters.
  String picture;
  if (!picture.reserve(81000)) { free(pixels); return false; }
  picture = "P1\n200 200\n";
  for (int y=0; y<200; y++) {
    for (int x=0; x<200; x++) picture += pixels[y*25+x/8] & (0x80>>(x%8)) ? "1 " : "0 ";
    picture += '\n';
  }
  free(pixels);
  return writeVerified(path,picture);
}

void renderSettingsView() {
  buddyHeader("SETTINGS");
  static const char *orientationNames[] = {"AUTO", "NORMAL", "FLIPPED"};
  char rows[SETTINGS_COUNT][32];
  static const char *soundNames[] = {"OFF", "LOW", "HIGH"};
  snprintf(rows[SETTINGS_SOUND],sizeof(rows[SETTINGS_SOUND]),"SOUND  %s",soundNames[buddySoundMode]);
  snprintf(rows[SETTINGS_CABLE_FRIENDS],sizeof(rows[SETTINGS_CABLE_FRIENDS]),
           "CABLE FRIENDS  %s",cableFriendsActive() ? "ON" : "OFF");
  snprintf(rows[SETTINGS_ORIENTATION],sizeof(rows[SETTINGS_ORIENTATION]),
           "ORIENTATION  %s",orientationNames[orientationMode]);
  for (uint8_t i=0;i<SETTINGS_COUNT;i++) {
    if (i==settingsSelection) buddyFill(4,34+i*30,192,23);
    buddyCentered(42+i*30,rows[i],1,i==settingsSelection);
  }
  char crashLabel[32];
  snprintf(crashLabel,sizeof(crashLabel),"CABLE CRASH: %s",
           buddyCableStageText(cableLastWatchdogStage));
  buddyCentered(160,cableModeError == CABLE_ERROR_WATCHDOG ? crashLabel :
    cableModeError != CABLE_ERROR_NONE ? buddyCableErrorText(cableModeError) :
    buddySoundSaveFailed ? "SOUND SAVE RETRY" :
    nearbyStorageLocked ? "FRIEND SAVE NEEDS REPAIR" :
    cableFriendsActive() ? "HOLD BOOT ON HOME TO EXIT" : "",1);
  buddyCentered(173,"SINGLE NEXT - HOLD CHANGE",1);
  buddyFill(4,181,192,16); buddyCentered(187,"DOUBLE CLICK HOME",1,true);
}

void renderSleepFace() {
  display->EPD_Clear();
  buddyRect(0,0,200,200);
  drawBuddyFace(true);
  buddyCentered(151,"GOODNIGHT",2);
  if (sleepSdCardPresent) buddyCentered(168,"SD SAFE TO SWAP",1);
  char line[28];
  snprintf(line,sizeof(line),"GEN %u  DAY %lu",buddy.generation,(unsigned long)buddyAgeDays(buddyNow()));
  buddyCentered(184,line,1);
}

void renderBuddyIntro() {
  if (introLocked) {
    buddyCentered(58,"WELCOME SAVE ERROR",1);
    buddyCentered(82,"LIFE NOT CREATED",1);
    buddyCentered(112,"RECOVERY NEEDED",1);
    return;
  }
  if (buddyIntro.step == INTRO_COMPLETE) {
    buddyCentered(60,"READY TO BEGIN",2);
    buddyCentered(100,clockFault ? "CLOCK NEEDS SETTING" : "SAVING NEW LIFE",1);
    return;
  }
  static const char *titles[] = {"BOY OR GIRL?", "FIRST KEEPSAKE", "FIRST SPARK"};
  static const char *options[3][4] = {
    {"BOY", "GIRL", "", ""},
    {"STONE", "PLANT", "NOOK", "TOY"},
    {"EXPLORER", "MAKER", "DREAMER", "FRIEND"}
  };
  buddyHeader("WELCOME");
  char step[16]; snprintf(step,sizeof(step),"STEP %u OF 3",buddyIntro.step+1);
  buddyCentered(35,step,1);
  buddyCentered(51,titles[buddyIntro.step],1);
  uint8_t count = introOptionCount(buddyIntro.step);
  int firstY = count == 2 ? 82 : 71;
  for (uint8_t i=0; i<count; i++) {
    int y = firstY + i*23;
    if (i == buddyIntro.cursor) buddyFill(8,y-4,184,18);
    buddyCentered(y,options[buddyIntro.step][i],1,i==buddyIntro.cursor);
  }
  buddyCentered(165,introSaveFailed ? "SAVE RETRY NEEDED" : "CLICK NEXT  DOUBLE BACK",1);
  buddyFill(4,181,192,16);
  buddyCentered(187,"HOLD TO CHOOSE",1,true);
}

void renderBuddy(bool forceFull) {
  bool forceCableGreeting = cableGreetingAwaitingPaint && cableFriendsActive() &&
    currentView == VIEW_BUDDY && nearbySceneVisible();
  bool fullRefresh = forceFull || forceCableGreeting;
  // Large layout changes get a fast white partial pass before the new frame.
  // The driver supplies old and new RAM planes for every partial waveform;
  // together these sharply reduce residue without a slow full-screen cycle.
  bool canCleanPartially = !fullRefresh && partialReady && partialCount + 1 < PARTIALS_BEFORE_FULL;
  if (cleanPartialRequested && canCleanPartially) {
    display->EPD_Clear();
    display->EPD_DisplayPart();
    partialCount++;
    delay(70);
  }
  display->EPD_Clear();
  buddyRect(0,0,200,200);
  if (introRequired && buddy.magic != BUDDY_MAGIC) {
    renderBuddyIntro();
    // Full refresh on new pages; option movement can use a cleaned partial.
    bool partial = !fullRefresh && partialReady && partialCount < PARTIALS_BEFORE_FULL;
    if (partial) { display->EPD_DisplayPart(); partialCount++; }
    else { display->EPD_Init(); display->EPD_Display(); display->EPD_Init_Partial(); partialReady=true; partialCount=0; }
    cleanPartialRequested=false; needsRender=false;
    return;
  }
  if (storageLocked || buddy.magic != BUDDY_MAGIC) {
    buddyCentered(65,storageLocked ? "SAVE NEEDS RECOVERY" : "CLOCK NEEDS SETTING",1);
    buddyCentered(92,"LIFE SAFELY PAUSED",1);
    display->EPD_Init(); display->EPD_Display(); cleanPartialRequested=false; needsRender=false;
    return;
  }
  switch (currentView) {
    case VIEW_ABOUT: renderAboutView(); break;
    case VIEW_SETTINGS: renderSettingsView(); break;
    default: renderHomeView(); break;
  }
  bool partial = !fullRefresh && partialReady && partialCount < PARTIALS_BEFORE_FULL;
  if (partial) {
    display->EPD_DisplayPart();
    partialCount++;
  } else {
    display->EPD_Init();
    display->EPD_Display();
    display->EPD_Init_Partial();
    partialReady = true;
    partialCount = 0;
  }
  if (cableGreetingAwaitingPaint && cableFriendsActive() &&
      currentView == VIEW_BUDDY && nearbySceneVisible()) {
    cableGreetingAwaitingPaint = false;
    cableGreetingUntilMs = millis() + 6000UL;
  }
  cleanPartialRequested = false;
  needsRender = false;
}
