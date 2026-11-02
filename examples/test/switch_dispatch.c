uint32_t main(void)
{
    int32_t choice = 3;
    int32_t result = 0;

    switch (choice)
    {
        case 1: result = 1; break;
        case 2: result = 2; break;
        case 3: result = 3;
        case 4: result += 4; break;
        default: result = 99; break;
    }

    if (result != 7)
    {
        printf("dense switch dispatch failed\n");
        return 1;
    }

    choice = 0;
    switch (choice)
    {
        case 1: result = 1; break;
        case 2: result = 2; break;
        case 3: result = 3; break;
        case 4: result = 4; break;
        default: result = 99; break;
    }

    if (result != 99)
    {
        printf("switch lower-bound dispatch failed\n");
        return 1;
    }

    choice = 5;
    switch (choice)
    {
        case 1: result = 1; break;
        case 2: result = 2; break;
        case 3: result = 3; break;
        case 4: result = 4; break;
        default: result = 99; break;
    }

    if (result != 99)
    {
        printf("switch upper-bound dispatch failed\n");
        return 1;
    }

    choice = 20;
    switch (choice)
    {
        case 1: result = 1; break;
        case 20: result = 20; break;
        case 100: result = 100; break;
        default: result = -1; break;
    }

    if (result != 20)
    {
        printf("sparse switch dispatch failed\n");
        return 1;
    }

    printf("switch dispatch passed\n");
    return 0;
}
