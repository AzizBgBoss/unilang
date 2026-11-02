/* https://github.com/jhhoward/MicroCity */

#define _WIN32 1

#include <stdint.h>
#include <stdbool.h>
#if !defined(_WIN32)
#include <avr/pgmspace.h>
#endif
#include "Draw.h"
#include "Interface.h"
#include "Game.h"
#include "Simulation.h"

uint8_t PowerGrid[1024];

uint8_t GetInput()
{
  uint8_t result = 0;
  uint8_t key = getkey();

  if (key & 1)        result |= INPUT_UP;
  if (key & 2)        result |= INPUT_DOWN;
  if (key & 4)        result |= INPUT_LEFT;
  if (key & 8)        result |= INPUT_RIGHT;
  if (key & 16)       result |= INPUT_A;
  if (key & 32)       result |= INPUT_B;

  return result;
}

void PutPixel(uint8_t x, uint8_t y, uint8_t colour)
{
  setpixel(x, y, colour);
}

void DrawBitmap(const uint8_t* bmp, uint8_t x, uint8_t y, uint8_t w, uint8_t h)
{
  for (uint8_t column = 0; column < w; ++column)
  {
    for (uint8_t row = 0; row < h; ++row)
    {
      const uint16_t byteIndex = (row >> 3) * w + column;
      const uint8_t pixels = pgm_read_byte(bmp + byteIndex);
      if (pixels & (1u << (row & 7)))
      {
        setpixel(x + column, y + row, 1);
      }
    }
  }
}

bool SaveCity()
{
  if (!save(&State, sizeof(State)))
  {
    printf("Could not save city\n");
    return false;
  }
  return true;
}

bool LoadCity()
{
  return load(&State, sizeof(State)) != 0;
}

uint8_t* GetPowerGrid()
{
  return PowerGrid;
}

int main()
{
  if (isflagsupported(0, 6))
  {
    flag(0, 6);
  }
  flag(1, 1);

  InitGame();
  LoadCity();

  while (1)
  {
    TickGame();
    refreshscreen();
    sleep(16);
  }

  return 0;
}
