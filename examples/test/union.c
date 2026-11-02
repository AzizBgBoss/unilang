typedef union Value {
    uint8_t small;
    uint32_t whole;
    uint8_t bytes[4];
    float real;
} Value;

union DirectTag {
    uint8_t first;
    uint32_t word;
};

typedef union Packed {
    uint8_t bytes[4];
    uint32_t word;
} Packed;

uint32_t main(void)
{
    Value value = {0x78};
    union DirectTag direct = {5};
    Packed packed = {{1, 2, 3, 4}};
    if (sizeof(Value) != 4 || sizeof(value) != 4 ||
        sizeof(union DirectTag) != 4 || direct.first != 5 ||
        value.small != 0x78 || packed.word != 0x01020304)
    {
        printf("union layout or initializer failed\n");
        return 1;
    }

    value.whole = 0x12345678;
    if (value.bytes[0] != 0x12)
    {
        printf("union overlapping storage failed\n");
        return 1;
    }

    value.bytes[3] = 0xAB;
    if (value.whole != 0x123456AB)
    {
        printf("union aliasing failed\n");
        return 1;
    }

    value.real = 1.5f;
    if (value.real < 1.49f || value.real > 1.51f)
    {
        printf("union floating member failed\n");
        return 1;
    }

    Value *pointer = &value;
    pointer->whole = 0xABCDEF12;
    if (value.whole != 0xABCDEF12)
    {
        printf("union pointer access failed\n");
        return 1;
    }

    printf("union ok\n");
    return 0;
}
