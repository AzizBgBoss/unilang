void count_to_three(uint32_t* result)
{
    uint32_t count = 0;
again:
    count++;
    if (count < 3)
        goto again;
    *result = count;
}

uint32_t main(void)
{
    uint32_t count = 0;
    count_to_three(&count);
    if (count != 3)
    {
        printf("goto failed\n");
        return 1;
    }

    long double root = sqrt(9.0);
    double quarter_turn = atan2(1.0, 0.0);
    if (root < 2.99 || root > 3.01
        || sin(0.0) != 0.0 || cos(0.0) < 0.99
        || quarter_turn < 1.56 || quarter_turn > 1.58)
    {
        printf("math functions failed\n");
        return 1;
    }

    printf("goto and math ok\n");
    return 0;
}
