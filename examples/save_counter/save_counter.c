uint32_t main(void)
{
    uint32_t count = 0;

    if (!load(&count, sizeof(count)))
        count = 0;

    count = count + 1;
    if (!save(&count, sizeof(count))) {
        printf("could not write save data\n");
        return 1;
    }

    uint32_t saved = 0;
    if (!load(&saved, sizeof(saved)) || saved != count) {
        printf("could not verify save data\n");
        return 1;
    }

    printf("This program has run %d time(s).\n", count);
    return 0;
}
