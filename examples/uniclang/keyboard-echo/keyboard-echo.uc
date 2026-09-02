uint32 main() {
    if (isflagsupported(0, 1)) {
        flag(0, 1);
        flag(1, 1);
    }

    while (1) {
        uint8 key = getkey();
        if (key != 0) {
            printf("pressed=%d\n", key);
        }
        sleep(50);
    }

    return 0;
}
