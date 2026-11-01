typedef unsigned char uint8_t;

uint8_t tone_sfx[68];

const uint8_t ending_pattern[4] = {
    0, 65, 66, 67
};

uint32_t main(void)
{
    flag(2, 2);

    tone_sfx[0] = 0;
    tone_sfx[1] = 8;
    tone_sfx[2] = 0;
    tone_sfx[3] = 0;
    for (int i = 0; i < 32; i++) {
        tone_sfx[4 + i * 2] = 0xF9;
        tone_sfx[5 + i * 2] = 0x0E;
    }

    if (!audioload(0, 0, tone_sfx, sizeof(tone_sfx))) {
        printf("could not load SFX\n");
        return 1;
    }
    if (!audioload(1, 0, ending_pattern, sizeof(ending_pattern))) {
        printf("could not load music pattern\n");
        return 1;
    }

    music(0);
    sleep(60);
    printf("music status: %d\n", audiostatus(-1));

    beep(440, 150);
    sleep(160);

    audiovolume(128);
    sfx(0, -1);
    sleep(100);
    audiopause();
    sleep(50);
    audioresume();
    sleep(100);
    audiostop();

    return 0;
}
