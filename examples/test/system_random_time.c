uint32_t main(void)
{
    uint32_t first;
    uint32_t second;
    uint32_t before;
    uint32_t after;

    srand(12345);
    first = rand(0, 100000);
    srand(12345);
    second = rand(0, 100000);
    if (first != second)
    {
        printf("srand determinism failed\n");
        return 1;
    }

    before = gettime();
    after = gettime();
    if (after < before)
    {
        printf("gettime monotonicity failed\n");
        return 1;
    }

    printf("system timing and random ok\n");
    return 0;
}
