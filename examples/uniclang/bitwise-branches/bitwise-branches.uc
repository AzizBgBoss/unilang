uint32 main() {
    uint8 value = 13;
    uint8 threshold = 7;
    uint8 guard = 0;

    if (value > threshold) {
        printf("value is above threshold\n");
        guard = 1;
    } else {
        printf("value is at or below threshold\n");
        guard = 0;
    }

    if (value == 13) {
        printf("exact match: 13\n");
    }

    if (value >= 10) {
        printf("value is in the high range\n");
    }

    if (guard != 0) {
        printf("guard is active\n");
    }

    return 0;
}
