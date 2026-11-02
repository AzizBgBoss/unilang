static uint8_t result;

void set_result(uint8_t value)
{
    result = value;
}

void clear_result(void)
{
    result = 0;
    return;
}

uint32_t main(void)
{
    set_result(42);
    if (result != 42)
    {
        printf("void function fallthrough failed\n");
        return 1;
    }

    clear_result();
    if (result != 0)
    {
        printf("void function return failed\n");
        return 1;
    }

    printf("void functions ok\n");
    return 0;
}
