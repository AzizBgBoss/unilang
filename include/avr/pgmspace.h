#pragma once

#include <stddef.h>
#include <stdint.h>

#define PROGMEM
#define PSTR(x) (x)
#define pgm_read_byte(x) romread((uint32_t)(x), 1)
#define pgm_read_word(x) romread((uint32_t)(x), 2)
#define pgm_read_ptr(x) romread((uint32_t)(x), 4)

static inline size_t strlen_P(const char* s)
{
  size_t len = 0;
  while (pgm_read_byte(s++) != 0)
  {
    ++len;
  }
  return len;
}
