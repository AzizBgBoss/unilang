uint32 main() {
    uint8 a = 255;
    uint8 b = 5;
    uint8 sum = a + b;
    printf("255 + 5 = %d\n", sum);

    uint8 x = 200;
    uint8 y = 100;
    uint8 z = x + y;
    printf("200 + 100 = %d\n", z);

    uint16 big = 65535;
    uint16 too_big = big + 1;
    printf("65535 + 1 = %d\n", too_big);

    return 0;
}
