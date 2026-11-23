#ifndef UNIAUDIO_H
#define UNIAUDIO_H

#include <stdint.h>
#include <string.h>

#define UNIAUDIO_SAMPLE_RATE 22050u
#define UNIAUDIO_CHANNELS 4u
#define UNIAUDIO_SFX_COUNT 64u
#define UNIAUDIO_SFX_NOTES 32u
#define UNIAUDIO_SFX_BYTES (4u + UNIAUDIO_SFX_NOTES * 2u)
#define UNIAUDIO_MUSIC_COUNT 64u
#define UNIAUDIO_MUSIC_BYTES 4u

// PCM voices play u8 samples (128 = silence) straight from the program ROM, so a
// voice only needs a pointer and a few counters (no sample buffers). Set this to 0
// to compile PCM out, or lower it on small hosts. It must not exceed 32.
#ifndef UNIAUDIO_PCM_VOICES
#define UNIAUDIO_PCM_VOICES 8u
#endif
#define UNIAUDIO_PCM_MAX_STEP 65535u // whole source samples consumed per output sample
#define UNIAUDIO_PCM_MAX_LENGTH 0x7FFFFFFFu
#define UNIAUDIO_PCM_FAIL 0xFFFFFFFFu
#define UNIAUDIO_PCM_LOOP 1u

#ifndef UNIAUDIO_READ_BYTE
#ifdef READ_FLASH_BYTE
#define UNIAUDIO_READ_BYTE(p) READ_FLASH_BYTE(p)
#else
#define UNIAUDIO_READ_BYTE(p) (*(const uint8_t *)(p))
#endif
#endif

enum UNIAUDIO_WAVEFORMS
{
    UNIAUDIO_TRIANGLE = 0,
    UNIAUDIO_TILTED_SAW,
    UNIAUDIO_SAW,
    UNIAUDIO_SQUARE,
    UNIAUDIO_PULSE,
    UNIAUDIO_ORGAN,
    UNIAUDIO_NOISE,
    UNIAUDIO_PHASER,
};

enum UNIAUDIO_EFFECTS
{
    UNIAUDIO_EFFECT_NONE = 0,
    UNIAUDIO_EFFECT_SLIDE,
    UNIAUDIO_EFFECT_VIBRATO,
    UNIAUDIO_EFFECT_DROP,
    UNIAUDIO_EFFECT_FADE_IN,
    UNIAUDIO_EFFECT_FADE_OUT,
    UNIAUDIO_EFFECT_ARP_FAST,
    UNIAUDIO_EFFECT_ARP_SLOW
};

typedef struct
{
    uint8_t bytes[UNIAUDIO_SFX_BYTES];
    uint8_t loaded;
} UniaudioSfx;

typedef struct
{
    uint8_t bytes[UNIAUDIO_MUSIC_BYTES];
    uint8_t loaded;
} UniaudioPattern;

typedef struct
{
    uint8_t active;
    uint8_t sfx;
    uint8_t note;
    uint8_t waveform;
    uint8_t volume;
    uint8_t effect;
    uint32_t samples_left;
    uint32_t samples_total;
    uint32_t phase;
    uint32_t phase_increment;
    uint32_t phase_increment_end;
    uint32_t noise;
    uint8_t custom;
} UniaudioChannel;

#if UNIAUDIO_PCM_VOICES > 0
typedef struct
{
    const uint8_t *data; // NULL = voice is free
    uint32_t length;
    uint32_t index;     // current source sample
    uint32_t frac;      // 0..UNIAUDIO_SAMPLE_RATE-1, fraction of a source sample
    uint32_t step_int;  // rate / UNIAUDIO_SAMPLE_RATE
    uint32_t step_frac; // rate % UNIAUDIO_SAMPLE_RATE
    uint8_t volume;
    uint8_t loop;
} UniaudioPcm;
#endif

typedef struct
{
    UniaudioSfx sfx[UNIAUDIO_SFX_COUNT];
    UniaudioPattern music[UNIAUDIO_MUSIC_COUNT];
    UniaudioChannel channels[UNIAUDIO_CHANNELS];
    uint8_t music_playing;
    uint8_t music_pattern;
    uint8_t music_loop_start;
    uint8_t music_loop_back;
    uint8_t music_stop;
    uint8_t music_channel_mask;
    uint8_t music_volume;
    uint8_t paused;
#if UNIAUDIO_PCM_VOICES > 0
    UniaudioPcm pcm[UNIAUDIO_PCM_VOICES];
    uint32_t pcm_mask; // bit n set = voice n is playing
#endif
    uint8_t beep_active;
    uint32_t beep_phase;
    uint32_t beep_increment;
    uint32_t beep_samples_left;
    uint32_t beep_samples_total;
} Uniaudio;

static inline uint32_t uniaudio_pitch_increment(uint8_t pitch)
{
    double frequency = 440.0;
    const double semitone = 1.0594630943592953;
    int steps = (int)pitch - 69;
    while (steps > 0)
    {
        frequency *= semitone;
        steps--;
    }
    while (steps < 0)
    {
        frequency /= semitone;
        steps++;
    }
    if (frequency >= (double)UNIAUDIO_SAMPLE_RATE)
        frequency = (double)UNIAUDIO_SAMPLE_RATE - 1.0;
    return (uint32_t)(frequency * 4294967296.0 / (double)UNIAUDIO_SAMPLE_RATE);
}

static inline void uniaudio_init(Uniaudio *audio)
{
    memset(audio, 0, sizeof(*audio));
    audio->music_volume = 255;
    for (uint32_t i = 0; i < UNIAUDIO_CHANNELS; i++)
        audio->channels[i].noise = 0x9E3779B9u ^ (i + 1u);
}

static inline int uniaudio_load(Uniaudio *audio, uint8_t kind, uint8_t index,
                                const uint8_t *data, uint32_t len)
{
    if (!audio || !data)
        return 0;

    if (kind == 0 && index < UNIAUDIO_SFX_COUNT &&
        len == UNIAUDIO_SFX_BYTES)
    {
        memcpy(audio->sfx[index].bytes, data, len);
        audio->sfx[index].loaded = 1;
        return 1;
    }

    if (kind == 1 && index < UNIAUDIO_MUSIC_COUNT &&
        len == UNIAUDIO_MUSIC_BYTES)
    {
        memcpy(audio->music[index].bytes, data, len);
        audio->music[index].loaded = 1;
        return 1;
    }

    return 0;
}

static inline uint16_t uniaudio_note_word(const uint8_t *sfx, uint8_t note)
{
    uint32_t offset = (uint32_t)note * 2u;
    return (uint16_t)(sfx[offset] | ((uint16_t)sfx[offset + 1u] << 8));
}

static inline uint32_t uniaudio_note_samples(const Uniaudio *audio, uint8_t sfx)
{
    uint32_t speed = audio->sfx[sfx].bytes[65];
    if (!speed)
        speed = 1;

    uint64_t samples = (uint64_t)speed * UNIAUDIO_SAMPLE_RATE / 120u;
    return samples ? (uint32_t)samples : 1u;
}

static inline void uniaudio_start_note(Uniaudio *audio, uint8_t channel)
{
    UniaudioChannel *voice = &audio->channels[channel];
    const uint8_t *sfx = audio->sfx[voice->sfx].bytes;
    if (voice->note >= UNIAUDIO_SFX_NOTES)
    {
        uint8_t loop_start = sfx[66] & 0x3F;
        uint8_t loop_end = sfx[67] & 0x3F;
        if (loop_end > loop_start && loop_end <= UNIAUDIO_SFX_NOTES)
            voice->note = loop_start;
        else
        {
            voice->active = 0;
            return;
        }
    }
    uint16_t note = uniaudio_note_word(sfx, voice->note);
    uint8_t pitch = (uint8_t)(note & 0x3Fu);
    voice->waveform = (uint8_t)((note >> 6) & 7u);
    voice->custom = (uint8_t)(note >> 15);
    voice->volume = (uint8_t)(((note >> 9) & 7u) * 255u / 7u);
    voice->effect = (uint8_t)((note >> 12) & 7u);
    voice->samples_total = uniaudio_note_samples(audio, voice->sfx);
    voice->samples_left = voice->samples_total;
    voice->phase_increment = uniaudio_pitch_increment((uint8_t)(pitch + 12u));
    voice->phase_increment_end = voice->phase_increment;
    if (voice->effect == UNIAUDIO_EFFECT_SLIDE && voice->note + 1u < UNIAUDIO_SFX_NOTES)
    {
        uint8_t next_pitch = (uint8_t)(uniaudio_note_word(sfx, (uint8_t)(voice->note + 1u)) & 0x3Fu);
        voice->phase_increment_end = uniaudio_pitch_increment((uint8_t)(next_pitch + 12u));
    }
    else if (voice->effect == UNIAUDIO_EFFECT_DROP)
        voice->phase_increment_end /= 2u;
    if (voice->custom && voice->waveform < 8)
    {
        const uint8_t *wave = audio->sfx[voice->waveform].bytes;

        if (audio->sfx[voice->waveform].loaded &&
            (wave[66] & 0x80u) &&
            (wave[65] & 1u))
        {
            voice->phase_increment >>= 1;
            voice->phase_increment_end >>= 1;

            if (!voice->phase_increment)
                voice->phase_increment = 1;
            if (!voice->phase_increment_end)
                voice->phase_increment_end = 1;
        }
    }
}

static inline void uniaudio_start_sfx(Uniaudio *audio, uint32_t index, int32_t requested_channel)
{
    if (index >= UNIAUDIO_SFX_COUNT || !audio->sfx[index].loaded)
        return;
    uint8_t channel = 0;
    if (requested_channel < 0)
    {
        for (channel = 0; channel < UNIAUDIO_CHANNELS; channel++)
            if (!audio->channels[channel].active)
                break;
        if (channel == UNIAUDIO_CHANNELS)
            channel = 0;
    }
    else if (requested_channel < (int32_t)UNIAUDIO_CHANNELS)
        channel = (uint8_t)requested_channel;
    else
        return;
    UniaudioChannel *voice = &audio->channels[channel];
    voice->active = 1;
    voice->sfx = (uint8_t)index;
    voice->note = 0;
    voice->phase = 0;
    uniaudio_start_note(audio, channel);
}

static inline void uniaudio_stop(Uniaudio *audio)
{
    for (uint32_t i = 0; i < UNIAUDIO_CHANNELS; i++)
        audio->channels[i].active = 0;
    audio->music_playing = 0;
    audio->music_channel_mask = 0;
    audio->beep_active = 0;
    audio->paused = 0;
#if UNIAUDIO_PCM_VOICES > 0
    for (uint32_t i = 0; i < UNIAUDIO_PCM_VOICES; i++)
        audio->pcm[i].data = NULL;
    audio->pcm_mask = 0;
#endif
}

static inline int uniaudio_any_channel(const Uniaudio *audio)
{
    for (uint32_t i = 0; i < UNIAUDIO_CHANNELS; i++)
        if (audio->channels[i].active)
            return 1;
    return 0;
}

static inline void uniaudio_play_pattern(Uniaudio *audio, uint8_t pattern)
{
    if (pattern >= UNIAUDIO_MUSIC_COUNT || !audio->music[pattern].loaded)
    {
        audio->music_playing = 0;
        audio->music_channel_mask = 0;
        return;
    }
    audio->music_pattern = pattern;
    const uint8_t *data = audio->music[pattern].bytes;
    if (data[0] & 0x80u)
        audio->music_loop_start = pattern;
    audio->music_loop_back = (uint8_t)((data[1] >> 7) & 1u);
    audio->music_stop = (uint8_t)((data[2] >> 7) & 1u);
    audio->music_channel_mask = 0;
    for (uint8_t channel = 0; channel < UNIAUDIO_CHANNELS; channel++)
    {
        audio->channels[channel].active = 0;
        uint8_t flags = data[channel];

        if (flags & 0x40u)
            continue;

        uint8_t sfx = flags & 0x3Fu;

        if (audio->sfx[sfx].loaded)
        {
            uniaudio_start_sfx(audio, sfx, channel);
            audio->music_channel_mask |= (uint8_t)(1u << channel);
        }
    }
    audio->music_playing = 1;
}

static inline void uniaudio_advance_music(Uniaudio *audio)
{
    if (audio->music_stop)
    {
        audio->music_playing = 0;
        return;
    }
    uint8_t next = audio->music_loop_back ? audio->music_loop_start
                                          : (uint8_t)(audio->music_pattern + 1u);
    if (next >= UNIAUDIO_MUSIC_COUNT || !audio->music[next].loaded)
    {
        audio->music_playing = 0;
        return;
    }
    uniaudio_play_pattern(audio, next);
}

// Starts a PCM clip (u8 samples, 128 = silence) read in place from `data`, which must stay
// valid while it plays (program ROM). `rate` is the clip's sample rate in Hz, any integer
// from 1 up to UNIAUDIO_SAMPLE_RATE * UNIAUDIO_PCM_MAX_STEP. A clip without UNIAUDIO_PCM_LOOP
// frees its voice by itself when it ends. If every voice is busy, the one closest to
// finishing is replaced (looping voices never finish). Returns the voice number or
// UNIAUDIO_PCM_FAIL.
static inline uint32_t uniaudio_pcm_play(Uniaudio *audio, const uint8_t *data, uint32_t length,
                                         uint32_t rate, uint32_t volume, uint32_t flags)
{
#if UNIAUDIO_PCM_VOICES > 0
    if (!data || length == 0 || length > UNIAUDIO_PCM_MAX_LENGTH ||
        rate == 0 || rate > UNIAUDIO_SAMPLE_RATE * UNIAUDIO_PCM_MAX_STEP)
        return UNIAUDIO_PCM_FAIL;
    uint32_t voice = UNIAUDIO_PCM_VOICES, best = 0;
    for (uint32_t i = 0; i < UNIAUDIO_PCM_VOICES; i++)
    {
        const UniaudioPcm *v = &audio->pcm[i];
        if (!v->data)
        {
            voice = i;
            break;
        }
        uint32_t remaining = v->loop ? UINT32_MAX : v->length - v->index;
        if (voice == UNIAUDIO_PCM_VOICES || remaining < best)
        {
            voice = i;
            best = remaining;
        }
    }
    UniaudioPcm *v = &audio->pcm[voice];
    v->length = length;
    v->index = 0;
    v->frac = 0;
    v->step_int = rate / UNIAUDIO_SAMPLE_RATE;
    v->step_frac = rate % UNIAUDIO_SAMPLE_RATE;
    v->volume = (uint8_t)(volume > 255u ? 255u : volume);
    v->loop = (flags & UNIAUDIO_PCM_LOOP) ? 1u : 0u;
    v->data = data;
    audio->pcm_mask |= 1u << voice;
    return voice;
#else
    (void)audio;
    (void)data;
    (void)length;
    (void)rate;
    (void)volume;
    (void)flags;
    return UNIAUDIO_PCM_FAIL;
#endif
}

static inline uint32_t uniaudio_command(Uniaudio *audio, uint8_t command, uint32_t a, uint32_t b)
{
    switch (command)
    {
    case 1:
    {
        if (a >= UNIAUDIO_SFX_COUNT || !audio->sfx[a].loaded)
            return UINT32_MAX;
        uint8_t channel;
        int32_t requested_channel = (int32_t)b;
        if (requested_channel < 0)
        {
            for (channel = 0; channel < UNIAUDIO_CHANNELS; channel++)
                if (!audio->channels[channel].active)
                    break;
            if (channel == UNIAUDIO_CHANNELS)
                channel = 0;
        }
        else if (requested_channel < (int32_t)UNIAUDIO_CHANNELS)
            channel = (uint8_t)requested_channel;
        else
            return UINT32_MAX;
        audio->music_channel_mask &= (uint8_t)~(1u << channel);
        uniaudio_start_sfx(audio, a, (int32_t)b);
        break;
    }
    case 2:
        if (a == UINT32_MAX)
        {
            for (uint8_t channel = 0; channel < UNIAUDIO_CHANNELS; channel++)
                if (audio->music_channel_mask & (1u << channel))
                    audio->channels[channel].active = 0;
            audio->music_playing = 0;
            audio->music_channel_mask = 0;
        }
        else if (a < UNIAUDIO_MUSIC_COUNT && audio->music[a].loaded)
            uniaudio_play_pattern(audio, (uint8_t)a);
        else
            return UINT32_MAX;
        break;
    case 3:
        uniaudio_stop(audio);
        break;
    case 4:
        audio->paused = 1;
        break;
    case 5:
        audio->paused = 0;
        break;
    case 6:
        audio->music_volume = (uint8_t)(a > 255u ? 255u : a);
        break;
    case 7:
        if (a == UINT32_MAX)
            return audio->music_playing ? audio->music_pattern : UINT32_MAX;
        if (a < UNIAUDIO_CHANNELS && audio->channels[a].active)
            return audio->channels[a].sfx;
        return UINT32_MAX;
#if UNIAUDIO_PCM_VOICES > 0
    case 8: // pcmstop(voice)
        if (a >= UNIAUDIO_PCM_VOICES)
            return UINT32_MAX;
        audio->pcm[a].data = NULL;
        audio->pcm_mask &= ~(1u << a);
        break;
    case 9: // pcmvolume(voice, volume)
        if (a >= UNIAUDIO_PCM_VOICES || !audio->pcm[a].data)
            return UINT32_MAX;
        audio->pcm[a].volume = (uint8_t)(b > 255u ? 255u : b);
        break;
    case 10: // pcmstatus(voice): 1 while playing, 0 when free
        if (a >= UNIAUDIO_PCM_VOICES)
            return UINT32_MAX;
        return audio->pcm[a].data ? 1u : 0u;
#endif
    default:
        return UINT32_MAX;
    }
    return 0;
}

static inline void uniaudio_beep(Uniaudio *audio, uint32_t frequency, uint32_t duration_ms)
{
    if (!frequency || !duration_ms)
    {
        audio->beep_active = 0;
        return;
    }
    if (frequency >= UNIAUDIO_SAMPLE_RATE)
        frequency = UNIAUDIO_SAMPLE_RATE - 1u;
    audio->beep_increment = (uint32_t)(((uint64_t)frequency << 32) / UNIAUDIO_SAMPLE_RATE);
    uint64_t samples = (uint64_t)duration_ms * UNIAUDIO_SAMPLE_RATE / 1000u;
    audio->beep_samples_total = samples > UINT32_MAX ? UINT32_MAX : (uint32_t)samples;
    if (!audio->beep_samples_total)
        audio->beep_samples_total = 1;
    audio->beep_samples_left = audio->beep_samples_total;
    audio->beep_phase = 0;
    audio->beep_active = 1;
}

static inline int32_t uniaudio_wave_sample(Uniaudio *audio, UniaudioChannel *voice)
{
    uint32_t phase = voice->phase;

    if (voice->custom)
    {
        uint8_t id = voice->waveform;

        if (id >= 8 || !audio->sfx[id].loaded)
            return 0;

        uint8_t index = (uint8_t)(phase >> 26);
        return (int32_t)(int8_t)audio->sfx[id].bytes[index] * 256;
    }

    switch (voice->waveform)
    {
    case UNIAUDIO_TRIANGLE:
    {
        uint32_t p = (phase >> 16) & 0xFFFFu;
        return p < 32768u ? -32768 + (int32_t)(p * 2u)
                          : 32767 - (int32_t)((p - 32768u) * 2u);
    }
    case UNIAUDIO_TILTED_SAW:
    {
        uint32_t p = phase >> 16;
        if (p < 49152u)
            return (int32_t)((uint64_t)p * 43690u / 49152u) - 32768;
        return (int32_t)((uint64_t)(p - 49152u) * 21845u / 16384u) - 32768;
    }
    case UNIAUDIO_SAW:
        return (int32_t)(phase >> 16) - 32768;
    case UNIAUDIO_SQUARE:
        return (phase & 0x80000000u) ? 24000 : -24000;
    case UNIAUDIO_PULSE:
        return (phase & 0xC0000000u) ? 18000 : -18000;
    case UNIAUDIO_ORGAN:
        return ((phase & 0x80000000u) ? 14000 : -14000) +
               ((phase & 0x40000000u) ? 6000 : -6000);
    case UNIAUDIO_NOISE:
        voice->noise ^= voice->noise << 13;
        voice->noise ^= voice->noise >> 17;
        voice->noise ^= voice->noise << 5;
        return (int16_t)(voice->noise >> 16);
    case UNIAUDIO_PHASER:
    {
        uint32_t p = phase + ((phase >> 5) & 0x0FFFFFFFu);
        uint32_t v = (p >> 16) & 0xFFFFu;
        return v < 32768u ? -32768 + (int32_t)(v * 2u)
                          : 32767 - (int32_t)((v - 32768u) * 2u);
    }
    default:
        return 0;
    }
}

static inline int16_t uniaudio_clamp_sample(int32_t sample)
{
    if (sample > 32767)
        return 32767;
    if (sample < -32768)
        return -32768;
    return (int16_t)sample;
}

static inline void uniaudio_render(Uniaudio *audio, int16_t *output, uint32_t samples)
{
    if (audio->paused)
    {
        memset(output, 0, (size_t)samples * sizeof(*output));
        return;
    }
    for (uint32_t i = 0; i < samples; i++)
    {
        int32_t mixed = 0;
        if (audio->beep_active)
        {
            mixed += (audio->beep_phase & 0x80000000u) ? 10000 : -10000;
            audio->beep_phase += audio->beep_increment;
            if (--audio->beep_samples_left == 0)
                audio->beep_active = 0;
        }
        for (uint8_t channel = 0; channel < UNIAUDIO_CHANNELS; channel++)
        {
            UniaudioChannel *voice = &audio->channels[channel];
            if (!voice->active)
                continue;
            int32_t sample = uniaudio_wave_sample(audio, voice);
            uint32_t volume = voice->volume;
            if (voice->effect == UNIAUDIO_EFFECT_FADE_IN)
                volume = (uint32_t)((uint64_t)volume * (voice->samples_total - voice->samples_left + 1u) / voice->samples_total);
            else if (voice->effect == UNIAUDIO_EFFECT_FADE_OUT)
                volume = (uint32_t)((uint64_t)volume * voice->samples_left / voice->samples_total);
            sample = (int32_t)((int64_t)sample * volume * audio->music_volume / (255u * 255u));
            mixed += sample;
            if ((voice->effect == UNIAUDIO_EFFECT_SLIDE || voice->effect == UNIAUDIO_EFFECT_DROP) &&
                voice->samples_total > 1u)
            {
                uint32_t elapsed = voice->samples_total - voice->samples_left;
                int64_t difference = (int64_t)voice->phase_increment_end - voice->phase_increment;
                uint32_t increment = (uint32_t)((int64_t)voice->phase_increment +
                                                difference * elapsed / (voice->samples_total - 1u));
                voice->phase += increment;
            }
            else if (voice->effect == UNIAUDIO_EFFECT_VIBRATO)
            {
                uint32_t elapsed = voice->samples_total - voice->samples_left;
                int32_t modulation = (elapsed / 64u) & 1u ? -(int32_t)(voice->phase_increment / 80u)
                                                          : (int32_t)(voice->phase_increment / 80u);
                voice->phase += (uint32_t)((int64_t)voice->phase_increment + modulation);
            }
            else if (voice->effect == UNIAUDIO_EFFECT_ARP_FAST ||
                     voice->effect == UNIAUDIO_EFFECT_ARP_SLOW)
            {
                uint32_t period = voice->effect == UNIAUDIO_EFFECT_ARP_FAST ? 96u : 192u;
                uint32_t semitones = ((voice->samples_total - voice->samples_left) / period) & 1u ? 7u : 0u;
                uint8_t pitch = (uint8_t)(uniaudio_note_word(audio->sfx[voice->sfx].bytes,
                                                             voice->note) &
                                          0x3Fu);
                voice->phase += uniaudio_pitch_increment((uint8_t)(pitch + 12u + semitones));
            }
            else
                voice->phase += voice->phase_increment;
            if (--voice->samples_left == 0)
            {
                voice->note++;
                const uint8_t *sfx = audio->sfx[voice->sfx].bytes;
                uint8_t loop_start = sfx[66] & 0x3F;
                uint8_t loop_end = sfx[67] & 0x3F;

                if (loop_end > loop_start &&
                    loop_end <= UNIAUDIO_SFX_NOTES &&
                    voice->note >= loop_end)
                {
                    voice->note = loop_start;
                }
                uniaudio_start_note(audio, channel);
            }
        }
#if UNIAUDIO_PCM_VOICES > 0
        if (audio->pcm_mask)
            for (uint32_t v = 0; v < UNIAUDIO_PCM_VOICES; v++)
            {
                UniaudioPcm *pcm = &audio->pcm[v];
                if (!pcm->data)
                    continue;
                mixed += ((int32_t)UNIAUDIO_READ_BYTE(pcm->data + pcm->index) - 128) * (int32_t)pcm->volume;
                pcm->index += pcm->step_int;
                pcm->frac += pcm->step_frac;
                if (pcm->frac >= UNIAUDIO_SAMPLE_RATE)
                {
                    pcm->frac -= UNIAUDIO_SAMPLE_RATE;
                    pcm->index++;
                }
                if (pcm->index >= pcm->length)
                {
                    if (pcm->loop)
                        pcm->index %= pcm->length;
                    else
                    {
                        pcm->data = NULL;
                        audio->pcm_mask &= ~(1u << v);
                    }
                }
            }
#endif
        output[i] = uniaudio_clamp_sample(mixed);
        if (audio->music_playing && !uniaudio_any_channel(audio))
            uniaudio_advance_music(audio);
    }
}

#endif
