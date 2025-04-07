
# EchoMidi Player

Simple MIDI player with audio effects (reverb, chorus, vibrato, tremolo, stereo widening, and echo).

**⚠️  Early Version - Work in Progress ⚠️**

Please note that this is an early version of the EchoMidi Player and is still under development.  Expect potential bugs and instability.

## Features

*   Plays MIDI files.
*   Applies real-time audio effects:
    *   **Reverb:** Adds a sense of space.
    *   **Chorus:** Creates a richer, wider sound.
    *   **Stereo Widening:** Enhances stereo separation.
    *   **Vibrato:** Adds pitch modulation for liveliness.
    *   **Tremolo:** Modulates volume for a pulsating effect.
    *   **Echo:** Simple sound repetition with delay.
*   Basic playback controls: Pause/Resume, Next/Previous track.
*   Effect toggles: Enable/disable effects during playback.
*   Cross-platform code base (Linux and Windows).

## Compilation

### Linux

```bash
gcc -o echomidi EchoMidi_player_v01.c -lSDL2 -lSDL2_mixer -lm
```

For optimized build (optional):

```bash
gcc -o echomidi EchoMidi_player_v01.c -lSDL2 -lSDL2_mixer -lm \
    -Ofast -flto=$(nproc) -march=native -mtune=native -mfpmath=sse \
    -falign-functions=16 -falign-loops=16 -fomit-frame-pointer -fno-ident -fno-asynchronous-unwind-tables -fvisibility=hidden -fno-plt \
    -ftree-vectorize -fopt-info-vec -fipa-pta -fipa-icf -fipa-cp-clone -funroll-loops -floop-interchange -fgraphite-identity -floop-nest-optimize -fmerge-all-constants \
    -fvariable-expansion-in-unroller -fno-stack-protector -Wl,-z,norelro -s -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--strip-all -pipe -DNDEBUG
```

### Windows (MinGW)

```bash
gcc -o echomidi.exe EchoMidi_player_v01.c -lmingw32 -lSDL2main -lSDL2 -lSDL2_mixer -lm
```

**Windows Build Status:**  While the code includes Windows compatibility adaptations, a successful Windows build has **not yet been verified**.  Use the Windows compilation instructions with caution and report any issues.

## Usage

1.  **SoundFont Bank:** Place a SoundFont file (.sf2) in the same directory as the executable. The player will automatically detect and use it.
    *   **Note:** The effect parameters in the code are currently tuned for the **"Nokia 3510.sf2"** MIDI bank. If you are using a different SoundFont, you may need to adjust the effect levels (reverb level, chorus level, etc.) directly within the `EchoMidi_player_v01.c` source code to achieve optimal results.

2.  **MIDI Files:** Place MIDI files (.mid) in the same directory.
3.  **Run the executable:** `./echomidi` (Linux) or `echomidi.exe` (Windows).

### Controls

*   **Right Arrow:** Next track
*   **Left Arrow:** Previous track
*   **P:** Pause / Resume
*   **Q:** Quit
*   **R:** Toggle Reverb (On/Off)
*   **C:** Toggle Chorus (On/Off)
*   **S:** Toggle Stereo Widening (On/Off)
*   **V:** Toggle Vibrato (On/Off)
*   **T:** Toggle Tremolo (On/Off)
*   **E:** Toggle Echo (On/Off)

## Dependencies

*   SDL2
*   SDL2\_mixer

Make sure you have these libraries installed on your system before compiling.

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

Copyright (c) Ivan Svarkovsky - 2025
