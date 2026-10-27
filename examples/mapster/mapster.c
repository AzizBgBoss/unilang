// https://github.com/AzizBgBoss/mapster
// uniClang port: no Arduino/EEPROM layer, direct VM calls only.

#include <stdbool.h>
#include <stdint.h>

#define WHITE 1
#define BLACK 0

#define KEY_UP 1
#define KEY_DOWN 2
#define KEY_LEFT 4
#define KEY_RIGHT 8
#define KEY_A 16

#define COIN 1
#define SEEDVILLAGER 2
#define FARMLAND 3
#define FRUITVILLAGER 4
#define HOEVILLAGER 5 // a hoe like in the Minecraft game not like in GTA
#define WATER 6
#define SAVE 100
#define LOAD 99

#define MAP_W 18
#define MAP_H 10
#define MAP_N 180

// flat arrays (the compiler only supports 1, 2 or 4 byte operands)
#define ME(x, y) mapelements[(x) * MAP_H + (y)]
#define TA(x, y) treeage[(x) * MAP_H + (y)]

// offsets into the sprites[] table
#define mapstone 0
#define mapplayery 32
#define mapplayerx 40
#define mapcoin 48
#define mapsvillager 56
#define mapfvillager 64
#define maphvillager 72
#define mapfarmland 80
#define maptree1 88
#define maptree2 96
#define maptree3 104
#define mapsave 112
#define mapload 120
#define mapwater 128

// game state
int8_t xOffset, yOffset;
bool isFacingY = true;
uint16_t money = 50;
uint8_t taxtime = 200;
uint8_t tax;
uint16_t seeds = 0, fruits = 0, hoes = 0;
uint16_t water = 1000;
int lasttime;
uint8_t keys;
uint8_t acd; // interact cooldown, in frames
uint8_t mapelements[MAP_N];
uint8_t treeage[MAP_N];

// save slot (in-memory, replaces the EEPROM emulation)
uint16_t s_money, s_seeds, s_fruits, s_hoes, s_water;
uint8_t s_taxtime, s_tax;
uint8_t s_map[MAP_N];
uint8_t s_age[MAP_N];

uint8_t sprites[136] = {
  // stone, 16x16 (2 bytes per row)
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11101111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111011,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111101, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  0b11111111, 0b11111111,
  // player y
  0b00000000, 0b00111100, 0b01000010, 0b11000011,
  0b11000011, 0b01000010, 0b00111100, 0b00000000,
  // player x
  0b00011000, 0b00111100, 0b01000010, 0b01000010,
  0b01000010, 0b01000010, 0b00111100, 0b00011000,
  // coin
  0b00000000, 0b00111100, 0b01000010, 0b01011010,
  0b01011010, 0b01000010, 0b00111100, 0b00000000,
  // seed villager
  0b00000000, 0b00111100, 0b01000010, 0b11000011,
  0b11000011, 0b01011010, 0b00111100, 0b00111100,
  // fruit villager
  0b00111100, 0b11000010, 0b11000010, 0b00111100,
  0b00000100, 0b00111100, 0b00100100, 0b00111100,
  // hoe villager
  0b00000000, 0b00111100, 0b01000010, 0b11000011,
  0b11000011, 0b10111100, 0b10000000, 0b10000000,
  // farmland
  0b11000100, 0b01010010, 0b00100100, 0b11011010,
  0b01010101, 0b10110110, 0b01010110, 0b11011011,
  // tree 1
  0b11000100, 0b00000000, 0b10000000, 0b00011000,
  0b00011001, 0b00000000, 0b00000000, 0b01011011,
  // tree 2
  0b11000100, 0b00000000, 0b00011000, 0b00111100,
  0b00111100, 0b00011000, 0b00000000, 0b01011011,
  // tree 3
  0b00000000, 0b00111100, 0b01111110, 0b01111110,
  0b01111110, 0b01111110, 0b00111100, 0b00000000,
  // save
  0b00111111, 0b01001101, 0b10001101, 0b10000001,
  0b10000001, 0b10111101, 0b10111101, 0b11111111,
  // load
  0b00011000, 0b00111100, 0b01011010, 0b00011000,
  0b00000000, 0b11111111, 0b00000000, 0b11111111,
  // water
  0b00111100, 0b01111110, 0b11111111, 0b11111111,
  0b10111101, 0b10000001, 0b10000001, 0b01111110,
};

// ---- drawing helpers ----
void pixel(int x, int y, int color) {
  if (x >= 0 && x < 128 && y >= 0 && y < 64) setpixel(x, y, color);
}

// 1bpp bitmap from sprites[off], rows padded to whole bytes
void drawBitmap(int x, int y, int off, int w, int h, int color) {
  int bpr = (w + 7) >> 3;
  for (int yy = 0; yy < h; yy++) {
    for (int xx = 0; xx < w; xx++) {
      int row = sprites[off + yy * bpr + (xx >> 3)];
      if ((row >> (7 - (xx & 7))) & 1) pixel(x + xx, y + yy, color);
    }
  }
}

void fillRect(int x, int y, int w, int h, int color) {
  for (int yy = 0; yy < h; yy++)
    for (int xx = 0; xx < w; xx++) pixel(x + xx, y + yy, color);
}

void drawRect(int x, int y, int w, int h, int color) {
  for (int xx = x; xx < x + w; xx++) {
    pixel(xx, y, color);
    pixel(xx, y + h - 1, color);
  }
  for (int yy = y; yy < y + h; yy++) {
    pixel(x, yy, color);
    pixel(x + w - 1, yy, color);
  }
}

// draws a number without numtostr (its buffer was not NUL-terminated, so stale digits showed up)
void drawNumber(uint32_t value, int x, int y) {
  int n = 1;
  uint32_t t = value;
  while (t >= 10) {
    t = t / 10;
    n++;
  }
  for (int i = n - 1; i >= 0; i--) {
    drawchar('0' + value % 10, x + i * 4, y, 1);
    value = value / 10;
  }
}

// ---- game logic ----
void saveGame() {
  s_money = money; s_seeds = seeds; s_fruits = fruits; s_hoes = hoes;
  s_water = water; s_taxtime = taxtime; s_tax = tax;
  for (int i = 0; i < MAP_N; i++) {
    s_map[i] = mapelements[i];
    s_age[i] = treeage[i];
  }
}

void loadGame() {
  money = s_money; seeds = s_seeds; fruits = s_fruits; hoes = s_hoes;
  water = s_water; taxtime = s_taxtime; tax = s_tax;
  for (int i = 0; i < MAP_N; i++) {
    mapelements[i] = s_map[i];
    treeage[i] = s_age[i];
  }
}

void interact(uint8_t x, uint8_t y) {
  uint8_t type = ME(x, y);
  if (type == SEEDVILLAGER) {
    if (money >= 3) {
      seeds++;
      money -= 3;
    }
  } else if (type == FARMLAND) {
    if (TA(x, y) == 0) {
      if (seeds > 0) {
        TA(x, y) = 1;
        seeds--;
      }
    } else if (TA(x, y) == 3) {
      TA(x, y) = 0;
      fruits += rand(3, 10);
    }
  } else if (type == FRUITVILLAGER) {
    if (fruits >= 3) {
      money += rand(3, 7);
      fruits -= 3;
    }
  } else if (type == HOEVILLAGER) {
    if (money >= 10) {
      money -= 10;
      hoes++;
    }
  } else if (type == WATER) {
    if (water + money * 1000 > 65535) water = 65535;
    else water += money * 1000;
    money = 0;
  } else if (type == SAVE) {
    saveGame();
  } else if (type == LOAD) {
    loadGame();
  } else if (type == 0) {
    if (hoes) {
      ME(x, y) = FARMLAND;
      hoes--;
    }
  }
}

void setup() {
  ME(0, 0) = COIN;
  ME(3, 3) = FRUITVILLAGER;
  ME(4, 5) = FARMLAND;
  ME(5, 2) = COIN;
  ME(5, 3) = SEEDVILLAGER;
  ME(7, 1) = LOAD;
  ME(7, 3) = SAVE;
  ME(9, 1) = WATER;
  ME(9, 3) = HOEVILLAGER;
  ME(16, 6) = COIN;

  drawstring("Mapster", 0, 0, 1);
  drawstring("By AzizBgBoss", 0, 18, 1);
  refreshscreen();
  sleep(1000);
  lasttime = gettime() / 1000;
}

void loop() {
  keys = getkey();
  if (acd > 0) acd--;

  if (keys & KEY_UP) {
    isFacingY = true;
    yOffset++;
  } else if (keys & KEY_DOWN) {
    isFacingY = true;
    yOffset--;
  }

  if (keys & KEY_LEFT) {
    isFacingY = false;
    xOffset++;
  } else if (keys & KEY_RIGHT) {
    isFacingY = false;
    xOffset--;
  }

  // growth tick
  if ((gettime() / 1000) - lasttime > 1) {
    for (uint8_t x = 0; x < MAP_W; x++) {
      for (uint8_t y = 0; y < MAP_H; y++) {
        if (ME(x, y) == FARMLAND && TA(x, y) > 0 && TA(x, y) < 3) {
          if (water > 0) {
            water--;
            if (rand(0, 100) == 69) TA(x, y)++;
          } else if (rand(0, 50) == 21) {
            TA(x, y)--;
          }
        }
      }
    }
    taxtime--;
    lasttime = gettime() / 1000;
  }

  if (taxtime < 1) {
    taxtime = 200;
    if (money >= tax) money -= tax;
    else money = 0;
    tax = rand(0, 11);
  }

  if (xOffset > 64) xOffset = 64;
  else if (xOffset < -80) xOffset = -80;
  if (yOffset > 32) yOffset = 32;
  else if (yOffset < -48) yOffset = -48;

  // ground and player
  for (int y = 0; y < 5; y++)
    for (int x = 0; x < 9; x++)
      drawBitmap(x * 16 + xOffset, y * 16 + yOffset, mapstone, 16, 16, WHITE);
  if (isFacingY) drawBitmap(60, 28, mapplayery, 8, 8, BLACK);
  else drawBitmap(60, 28, mapplayerx, 8, 8, BLACK);

  // tiles
  for (uint8_t x = 0; x < MAP_W; x++) {
    for (uint8_t y = 0; y < MAP_H; y++) {
      int tx = x * 8 + xOffset;
      int ty = y * 8 + yOffset;
      if (xOffset <= (x * (-8)) + 64 && xOffset > (x * (-8)) + 57 && yOffset <= (y * (-8)) + 32 && yOffset > (y * (-8)) + 25) {
        if ((keys & KEY_A) && acd == 0) {
          interact(x, y);
          acd = 12;
        }
        if (ME(x, y) == COIN) {
          money++;
          ME(x, y) = 0;
        }
      }
      switch (ME(x, y)) {
        case COIN: drawBitmap(tx, ty, mapcoin, 8, 8, BLACK); break;
        case SEEDVILLAGER: drawBitmap(tx, ty, mapsvillager, 8, 8, BLACK); break;
        case FRUITVILLAGER: drawBitmap(tx, ty, mapfvillager, 8, 8, BLACK); break;
        case HOEVILLAGER: drawBitmap(tx, ty, maphvillager, 8, 8, BLACK); break;
        case FARMLAND:
          if (TA(x, y) == 0) drawBitmap(tx, ty, mapfarmland, 8, 8, BLACK);
          else if (TA(x, y) == 1) drawBitmap(tx, ty, maptree1, 8, 8, BLACK);
          else if (TA(x, y) == 2) drawBitmap(tx, ty, maptree2, 8, 8, BLACK);
          else if (TA(x, y) == 3) drawBitmap(tx, ty, maptree3, 8, 8, BLACK);
          break;
        case SAVE: drawBitmap(tx, ty, mapsave, 8, 8, BLACK); break;
        case LOAD: drawBitmap(tx, ty, mapload, 8, 8, BLACK); break;
        case WATER: drawBitmap(tx, ty, mapwater, 8, 8, BLACK); break;
      }
    }
  }

  // HUD: money, seeds, fruits, hoes, tax, tax timer
  fillRect(1, 1, 127, 17, BLACK);
  drawRect(0, 0, 128, 19, WHITE);
  // glyphs are 4 px wide in this VM: row 1 = $ seeds fruits hoes, row 2 = tax, timer, water
  drawstring("$", 2, 2, 1);
  drawNumber(money, 7, 2);
  drawstring("S", 36, 2, 1);
  drawNumber(seeds, 41, 2);
  drawstring("F", 64, 2, 1);
  drawNumber(fruits, 69, 2);
  drawstring("H", 92, 2, 1);
  drawNumber(hoes, 97, 2);
  drawstring("TAX", 2, 10, 1);
  drawNumber(tax, 16, 10);
  drawstring("T", 36, 10, 1);
  drawNumber(taxtime, 41, 10);
  drawstring("W", 64, 10, 1);
  drawNumber(water, 69, 10);

  refreshscreen();
  sleep(10);
}

uint32_t main() {
  if (isflagsupported(0, 4)) {
    flag(0, 4);
  } else {
    printf("Graphics flag 4 unsupported\n");
    exit();
  }

  if (isflagsupported(1, 1)) {
    flag(1, 1);
  } else {
    printf("Keyboard flag 1 unsupported\n");
  }

  setup();
  while (true) {
    loop();
  }
  return 0;
}
