// u8 PCM clips (128 = silence) played from ROM in audio mode 3
const uint8_t tone[64] = {
    0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
    0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0, 0xC0,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40,
    0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40, 0x40
};
const uint8_t slow[4] = {128, 255, 128, 0};

uint32_t main(void)
{
    flag(FLAG_AUDIO, AUDIO_PCM);

    // a clip that is not looped frees its voice by itself when it ends (64 samples, about 3 ms; the
    // wait is long because a real host only runs the mixer once per audio buffer)
    uint8_t v = pcmplay(tone, 64, 22050, 255, 0);
    if (v > 7 || pcmstatus(v) != 1)
    {
        printf("pcmplay failed\n");
        return 1;
    }
    sleep(250);
    if (pcmstatus(v) != 0)
    {
        printf("voice was not freed\n");
        return 2;
    }

    // any integer rate works: 4 samples at 1 Hz take 4 seconds
    uint8_t s = pcmplay(slow, 4, 1, 128, 0);
    sleep(100);
    if (pcmstatus(s) != 1)
    {
        printf("1 Hz clip ended too early\n");
        return 3;
    }
    pcmstop(s);
    if (pcmstatus(s) != 0)
    {
        printf("pcmstop failed\n");
        return 4;
    }

    // looping clips keep their voice until stopped; audiostop() stops everything
    uint8_t l = pcmplay(tone, 64, 8000, 200, PCM_LOOP);
    sleep(100);
    if (pcmstatus(l) != 1)
    {
        printf("looping clip ended\n");
        return 5;
    }
    pcmvolume(l, 50);
    audiostop();
    if (pcmstatus(l) != 0)
    {
        printf("audiostop kept a voice\n");
        return 6;
    }

    // invalid rate or size, and an invalid voice number
    if (pcmplay(tone, 64, 0, 255, 0) != 255 || pcmplay(tone, 0, 22050, 255, 0) != 255)
    {
        printf("invalid pcmplay accepted\n");
        return 7;
    }
    if (pcmstatus(200) != 4294967295)
    {
        printf("invalid voice accepted\n");
        return 8;
    }

    // more clips than voices: the extra one replaces a voice instead of failing
    for (uint32_t i = 0; i < 8; i++)
        pcmplay(tone, 64, 22050, 10, PCM_LOOP);
    if (pcmplay(tone, 64, 22050, 10, PCM_LOOP) > 7)
    {
        printf("voice stealing failed\n");
        return 9;
    }
    audiostop();

    // PCM needs mode 3
    flag(FLAG_AUDIO, AUDIO_ENGINE);
    if (pcmplay(tone, 64, 22050, 255, 0) != 255)
    {
        printf("pcmplay worked outside mode 3\n");
        return 10;
    }

    printf("pcm ok\n");
    return 0;
}
