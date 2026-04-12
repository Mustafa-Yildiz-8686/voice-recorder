#include <iostream>
#include <iomanip>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cmath>
#include <ctime>
#include <string>
#include <algorithm>
#include <sys/select.h>
#include <unistd.h>
#include <portaudio.h>
#include <sndfile.h>

// ---------------------------------------------------------------------------
// Config — single source of truth for all tunable parameters
// ---------------------------------------------------------------------------
struct Config {
    int         sampleRate      = 44100;
    int         framesPerBuffer = 512;
    int         numChannels     = 1;
    std::string outputFile      = "";   // empty = auto-generate from timestamp

    // VU meter
    int vuMeterWidth = 40;  // bar width in characters
    int vuUpdateHz   = 20;  // refresh rate
};

// ---------------------------------------------------------------------------
// Generate a timestamped filename: recording_YYYY-MM-DD_HH-MM-SS.wav
// ---------------------------------------------------------------------------
std::string makeTimestampedFilename() {
    std::time_t now = std::time(nullptr);
    std::tm *t = std::localtime(&now);
    char buf[64];
    std::strftime(buf, sizeof(buf), "recording_%Y-%m-%d_%H-%M-%S.wav", t);
    return std::string(buf);
}

// ---------------------------------------------------------------------------
// Atomic flags
// ---------------------------------------------------------------------------
std::atomic<bool>  keepRecording(true);

// Peak level in [0,1] — written by recording loop, read+reset by VU thread
std::atomic<float> peakLevel(0.0f);

// ---------------------------------------------------------------------------
// SIGINT: clean stop on Ctrl-C
// ---------------------------------------------------------------------------
void signalHandler(int /*signum*/) {
    keepRecording = false;
}

// ---------------------------------------------------------------------------
// Input thread: polls stdin with a timeout so we never hang if the main
// loop exits for another reason
// ---------------------------------------------------------------------------
void inputThread() {
    while (keepRecording) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(STDIN_FILENO, &fds);

        struct timeval tv{};
        tv.tv_usec = 100'000; // 100 ms

        int ret = select(STDIN_FILENO + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0) { std::cin.get(); keepRecording = false; break; }
        if (ret < 0) break;
    }
}

// ---------------------------------------------------------------------------
// VU meter thread
//
// Redraws a single terminal line using ANSI escape codes:
//   \r       — carriage return (stay on same line)
//   \033[K   — erase to end of line
//   \033[32m — green  \033[33m — yellow  \033[31m — red  \033[0m — reset
// ---------------------------------------------------------------------------
void vuMeterThread(const Config &cfg) {
    const int width    = cfg.vuMeterWidth;
    const int sleepMs  = 1000 / cfg.vuUpdateHz;
    const int yellowAt = static_cast<int>(width * 0.70f);
    const int redAt    = static_cast<int>(width * 0.90f);

    while (keepRecording) {
        float level = peakLevel.exchange(0.0f);
        int   bars  = std::clamp(static_cast<int>(level * width), 0, width);

        // Build bar string
        std::string meter(width, ' ');
        for (int i = 0; i < bars; ++i) meter[i] = '=';
        if (bars > 0) meter[bars - 1] = '>';

        // Split bar into colour zones
        int g = std::min(bars, yellowAt);
        int y = std::max(0, std::min(bars, redAt) - yellowAt);
        int r = std::max(0, bars - redAt);

        std::cout
            << "\r\033[K"
            << "Level: ["
            << "\033[32m" << meter.substr(0, g)             << "\033[0m"
            << "\033[33m" << meter.substr(yellowAt, y)       << "\033[0m"
            << "\033[31m" << meter.substr(redAt, r)          << "\033[0m"
            << std::string(width - bars, ' ')
            << "] "
            << std::fixed << std::setprecision(1) << (level * 100.0f) << "%"
            << "  (Enter/Ctrl-C to stop)"
            << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
    }

    std::cout << "\r\033[K" << std::flush; // clear meter line on exit
}

// ---------------------------------------------------------------------------
// RAII guards — ensure cleanup on any exit path
// ---------------------------------------------------------------------------
struct SndfileGuard {
    SNDFILE *f = nullptr;
    explicit SndfileGuard(SNDFILE *sf) : f(sf) {}
    ~SndfileGuard() { if (f) sf_close(f); }
    SndfileGuard(const SndfileGuard &) = delete;
    SndfileGuard &operator=(const SndfileGuard &) = delete;
};

struct PortAudioGuard {
    PaStream *stream  = nullptr;
    bool      started = false;
    ~PortAudioGuard() {
        if (stream) {
            if (started) Pa_StopStream(stream);
            Pa_CloseStream(stream);
        }
        Pa_Terminate();
    }
    PortAudioGuard() = default;
    PortAudioGuard(const PortAudioGuard &) = delete;
    PortAudioGuard &operator=(const PortAudioGuard &) = delete;
};

// ---------------------------------------------------------------------------
// Peak level of a sample buffer, normalised to [0, 1]
// ---------------------------------------------------------------------------
float computePeak(const short *buf, int samples) {
    short peak = 0;
    for (int i = 0; i < samples; ++i) {
        short v = static_cast<short>(std::abs(buf[i]));
        if (v > peak) peak = v;
    }
    return static_cast<float>(peak) / 32767.0f;
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
int main() {
    std::signal(SIGINT, signalHandler);

    Config cfg;
    if (cfg.outputFile.empty())
        cfg.outputFile = makeTimestampedFilename();

    // --- PortAudio ---
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio init error: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    PortAudioGuard paGuard;

    err = Pa_OpenDefaultStream(&paGuard.stream,
                               cfg.numChannels,
                               0,       // no output
                               paInt16,
                               cfg.sampleRate,
                               cfg.framesPerBuffer,
                               nullptr, // blocking API, no callback
                               nullptr);
    if (err != paNoError) {
        std::cerr << "PortAudio open error: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }

    err = Pa_StartStream(paGuard.stream);
    if (err != paNoError) {
        std::cerr << "PortAudio start error: " << Pa_GetErrorText(err) << "\n";
        return 1;
    }
    paGuard.started = true;

    // --- libsndfile ---
    SF_INFO sfinfo{};
    sfinfo.channels   = cfg.numChannels;
    sfinfo.samplerate = cfg.sampleRate;
    sfinfo.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE *rawFile = sf_open(cfg.outputFile.c_str(), SFM_WRITE, &sfinfo);
    if (!rawFile) {
        std::cerr << "Error opening " << cfg.outputFile << ": "
                  << sf_strerror(nullptr) << "\n";
        return 1;
    }
    SndfileGuard sfGuard(rawFile);

    std::cout << "Recording started → " << cfg.outputFile << "\n";

    // --- Threads ---
    std::thread inputThr(inputThread);
    std::thread vuThr(vuMeterThread, std::cref(cfg));

    // --- Recording loop ---
    const int  bufSamples = cfg.framesPerBuffer * cfg.numChannels;
    short      buffer[bufSamples];
    sf_count_t totalFrames = 0;

    while (keepRecording) {
        err = Pa_ReadStream(paGuard.stream, buffer, cfg.framesPerBuffer);

        if (err == paInputOverflowed) {
            peakLevel.store(0.0f); // show silence for this window
            continue;
        }
        if (err != paNoError) {
            std::cerr << "\nPortAudio read error: " << Pa_GetErrorText(err) << "\n";
            keepRecording = false;
            break;
        }

        // Push peak to VU meter (keep the higher value if meter hasn't consumed yet)
        float newPeak = computePeak(buffer, bufSamples);
        float cur = peakLevel.load();
        while (newPeak > cur && !peakLevel.compare_exchange_weak(cur, newPeak));

        // Write frames
        sf_count_t written = sf_write_short(rawFile, buffer, cfg.framesPerBuffer);
        if (written != cfg.framesPerBuffer) {
            std::cerr << "\nsndfile write error: expected " << cfg.framesPerBuffer
                      << " frames, got " << written << "\n";
            keepRecording = false;
            break;
        }
        totalFrames += written;
    }

    // --- Teardown ---
    keepRecording = false;
    if (vuThr.joinable())    vuThr.join();
    if (inputThr.joinable()) inputThr.join();

    sf_write_sync(rawFile);

    double duration = static_cast<double>(totalFrames) / cfg.sampleRate;
    std::cout << "Done. Recorded " << totalFrames << " frames ("
              << std::fixed << std::setprecision(2) << duration
              << " s) → " << cfg.outputFile << "\n";

    return 0;
}