#include <stdio.h>

#define ULVM_MEM_SIZE 256
#include "unilang.c"

UnilangVM vm;

const char program[] = {
    0x08, 0x00, 0x03, 'H', 'i', '!',
};

#define PROGRAM_SIZE (sizeof(program) / sizeof(program[0]))

void outputchar(char c)
{
    printf("%c", c);
}

uint8_t readbyte(uint32_t addr)
{
    return program[addr];
};

int main()
{
    ULVM_init(&vm, outputchar, readbyte);
    while (vm.pc < PROGRAM_SIZE)
    {
        ULVM_handlenextinstruction(&vm);
    }
    return 0;
}