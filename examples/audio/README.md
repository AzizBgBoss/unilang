# Audio demo

Compile and run the sample from the repository root:

```sh
python compiler.py examples/audio/audio.c -o examples/audio/audio.ulc
C-interpreter/main examples/audio/audio.ulc
```

The sample enables audio flag 2 value `2`, builds and loads a PICO-8-format
SFX record and music pattern, and exercises music/SFX playback, `audiostatus`,
`beep`, volume, pause/resume, and stop.

To render a WAV file without an SDL audio device, build the headless host and
run:

```sh
cc -O2 -std=c11 -I C-interpreter tools/headless.c -o tools/headless
tools/headless examples/audio/audio.ulc -wav examples/audio/audio.wav
```

The headless host advances synthesized audio during `sleep` and frame refresh
calls. The record layout and audio command arguments are described in the
root README under compiler builtins.
