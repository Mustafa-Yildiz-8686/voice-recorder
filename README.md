# 🎙️ recorder

> A lightweight, no-frills CLI audio recorder for Linux and macOS — written in C++17 using PortAudio and libsndfile.

Records from your default microphone to a timestamped `.wav` file with a live colour VU meter in the terminal. Press **Enter** or **Ctrl-C** to stop.

```
Recording started → recording_2026-04-12_14-30-00.wav
Level: [=============================>          ] 74.3%  (Enter/Ctrl-C to stop)
```

---

## Features

- 🕐 **Timestamped output files** — never overwrites a previous recording (`recording_YYYY-MM-DD_HH-MM-SS.wav`)
- 📊 **Live VU meter** — colour-coded bar (green → yellow → red) redrawn in-place on a single terminal line
- 🧹 **Clean shutdown** — both Enter and Ctrl-C flush and close the file gracefully before exiting
- ⚙️ **Single config struct** — all tunable parameters in one place at the top of the file, no scattered `#define`s
- 🔒 **RAII resource management** — PortAudio stream and sndfile handle are always released, even on error paths

---

## Dependencies

| Library | Purpose | Package (Debian/Ubuntu) |
|---|---|---|
| [PortAudio](http://www.portaudio.com/) | Cross-platform audio I/O | `libportaudio2` `libportaudio-dev` |
| [libsndfile](http://libsndfile.github.io/libsndfile/) | WAV file encoding | `libsndfile1` `libsndfile1-dev` |

### Check if already installed

```bash
dpkg -l | grep portaudio
dpkg -l | grep libsndfile
```

You need all four packages — the runtime lib **and** the `-dev` package for each. The `-dev` packages provide the headers (`portaudio.h`, `sndfile.h`) the compiler needs.

### Install

**Ubuntu / Debian**
```bash
sudo apt install libportaudio2 libportaudio-dev libsndfile1 libsndfile1-dev
```

**macOS (Homebrew)**
```bash
brew install portaudio libsndfile
```

---

## Building

```bash
g++ recorder.cpp -o recorder -lportaudio -lsndfile -lpthread -std=c++17
```

**macOS (Homebrew, Apple Silicon)**
```bash
g++ recorder.cpp -o recorder \
  -I/opt/homebrew/include \
  -L/opt/homebrew/lib \
  -lportaudio -lsndfile -lpthread -std=c++17
```

> On Intel Macs replace `/opt/homebrew` with `/usr/local`.

Or just use the included `Makefile`:

```bash
make
```

---

## Usage

```bash
./recorder
```

Recording starts immediately and outputs a file like `recording_2026-04-12_14-30-00.wav` in the current directory.

```
Recording started → recording_2026-04-12_14-30-00.wav
Level: [============================>           ] 71.2%  (Enter/Ctrl-C to stop)

Done. Recorded 1058304 frames (24.00 s) → recording_2026-04-12_14-30-00.wav
```

Stop recording at any time with **Enter** or **Ctrl-C**. The file is always properly flushed and closed before the process exits.

---

## Configuration

All settings live in the `Config` struct near the top of `recorder.cpp`:

```cpp
struct Config {
    int         sampleRate      = 44100;   // Hz
    int         framesPerBuffer = 512;     // samples per read chunk
    int         numChannels     = 1;       // 1 = mono, 2 = stereo
    std::string outputFile      = "";      // empty = auto timestamped filename

    int vuMeterWidth = 40;   // width of the VU bar in characters
    int vuUpdateHz   = 20;   // how many times per second the meter refreshes
};
```

To hard-code a filename instead of auto-generating one, set:
```cpp
cfg.outputFile = "my_recording.wav";
```

---

## Output format

| Property | Value |
|---|---|
| Format | WAV (PCM) |
| Bit depth | 16-bit signed |
| Sample rate | 44100 Hz (default) |
| Channels | Mono (default) |

---

## Troubleshooting

**`undefined reference` errors when compiling**
You're missing one of the `-l` linker flags. Make sure you have all four packages installed and use the full compile command above.

**`No input devices found` at runtime (Linux)**
Your user may not be in the `audio` group:
```bash
sudo usermod -aG audio $USER
# log out and back in for it to take effect
```

**Headers not found on macOS**
Add the Homebrew include path explicitly with `-I/opt/homebrew/include` (see macOS build command above).

---

## Roadmap

- [ ] Command-line arguments (`--rate`, `--channels`, `--output`, `--duration`)
- [ ] Output format selection (FLAC, OGG)
- [ ] Silence detection / auto-pause
- [ ] Callback-based PortAudio with lock-free ring buffer for lower latency
- [ ] ncurses TUI with waveform visualiser
- [ ] Whisper transcription on stop

---

## License

MIT
