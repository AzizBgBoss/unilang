uint32_t main(void)
{
    uint32_t first;
    uint32_t second;

    push(17);
    push(29);
    first = pop();
    second = pop();

    if (first != 29 || second != 17)
    {
        printf("value stack test failed\n");
        return 1;
    }

    printf("value stack test passed\n");
    return 0;
}
