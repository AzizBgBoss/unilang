/* https://github.com/jhhoward/MicroCity */

#include <stdint.h>
#include <stdbool.h>
#include <avr/pgmspace.h>
#include "Draw.h"
#include "Interface.h"
#include "Game.h"
#include "Simulation.h"

uint8_t PowerGrid[1024];
uint8_t SavedCity[sizeof(GameState)];
uint8_t SavedValid = 0;

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
      const uint8_t byteIndex = (row >> 3) * w + column;
      const uint8_t pixels = pgm_read_byte(bmp + byteIndex);
      if (pixels & (1u << (row & 7)))
      {
        setpixel(x + column, y + row, 1);
      }
    }
  }
}

void SaveCity()
{
  // uniClang has no EEPROM, so this is a RAM-only fake save slot.
  uint8_t* ptr = (uint8_t*)&State;
  for (size_t n = 0; n < sizeof(GameState); ++n)
  {
    SavedCity[n] = ptr[n];
  }
  SavedValid = 1;
}

bool LoadCity()
{
  if (!SavedValid)
  {
    return false;
  }

  uint8_t* ptr = (uint8_t*)&State;
  for (size_t n = 0; n < sizeof(GameState); ++n)
  {
    ptr[n] = SavedCity[n];
  }

  return true;
}

uint8_t* GetPowerGrid()
{
  return PowerGrid;
}

int main()
{
  if (isflagsupported(0, 4))
  {
    flag(0, 4);
  }
  flag(1, 1);

  InitGame();

  while (1)
  {
    TickGame();
    refreshscreen();
    sleep(40);
  }

  return 0;
}
