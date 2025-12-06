#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <portaudio.h>
#include <sndfile.h>

#define SAMPLE_RATE 44100
#define FRAMES_PER_BUFFER 512
#define NUM_CHANNELS 1

// Atomic flag to control recording
std::atomic<bool> keepRecording(true);

void inputThread() {
    std::cout << "Press Enter to stop recording..." << std::endl;
    std::cin.get(); // Wait for Enter key
    keepRecording = false;
}

int main() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        std::cerr << "PortAudio init error: " << Pa_GetErrorText(err) << std::endl;
        return 1;
    }

    // Open default input stream
    PaStream *stream;
    err = Pa_OpenDefaultStream(&stream,
                               NUM_CHANNELS,    // input channels
                               0,              // no output channels
                               paInt16,        // 16-bit signed int samples
                               SAMPLE_RATE,
                               FRAMES_PER_BUFFER,
                               nullptr,        // no callback, use blocking API
                               nullptr);       // no callback userData

    if (err != paNoError) {
        std::cerr << "PortAudio open error: " << Pa_GetErrorText(err) << std::endl;
        Pa_Terminate();
        return 1;
    }

    // Start the audio stream
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        std::cerr << "PortAudio start error: " << Pa_GetErrorText(err) << std::endl;
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }

    // Setup libsndfile for WAV output
    SF_INFO sfinfo;
    sfinfo.channels = NUM_CHANNELS;
    sfinfo.samplerate = SAMPLE_RATE;
    sfinfo.format = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

    SNDFILE *outfile = sf_open("output.wav", SFM_WRITE, &sfinfo);
    if (!outfile) {
        std::cerr << "Error opening output.wav for writing: " << sf_strerror(nullptr) << std::endl;
        Pa_StopStream(stream);
        Pa_CloseStream(stream);
        Pa_Terminate();
        return 1;
    }

    std::cout << "Recording started..." << std::endl;

    // Start input monitoring thread
    std::thread inputThreadObj(inputThread);

    // Allocate buffer with proper size for the number of channels
    short buffer[FRAMES_PER_BUFFER * NUM_CHANNELS];

    // Main recording loop
    while (keepRecording) {
        // Check if stream is still active
        if (Pa_IsStreamActive(stream) != 1) {
            std::cerr << "Stream is no longer active" << std::endl;
            break;
        }

        // Read audio data
        err = Pa_ReadStream(stream, buffer, FRAMES_PER_BUFFER);
        if (err == paInputOverflowed) {
            // Input overflow is not fatal, just continue
            std::cerr << "Warning: Input overflow detected" << std::endl;
        } else if (err != paNoError) {
            std::cerr << "PortAudio read error: " << Pa_GetErrorText(err) << std::endl;
            break;
        }

        // Write audio data to file
        sf_count_t frames_written = sf_write_short(outfile, buffer, FRAMES_PER_BUFFER * NUM_CHANNELS);
        if (frames_written != (FRAMES_PER_BUFFER * NUM_CHANNELS)) {
            std::cerr << "Error writing to file. Expected " << (FRAMES_PER_BUFFER * NUM_CHANNELS)
                      << " samples, wrote " << frames_written << std::endl;
            break;
        }

        // Small sleep to prevent excessive CPU usage
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    // Cleanup
    std::cout << "Stopping recording..." << std::endl;

    // Signal input thread to stop and wait for it
    keepRecording = false;
    if (inputThreadObj.joinable()) {
        inputThreadObj.join();
    }

    // Close audio file
    if (sf_close(outfile) != 0) {
        std::cerr << "Error closing output file" << std::endl;
    }

    // Stop and close PortAudio stream
    err = Pa_StopStream(stream);
    if (err != paNoError) {
        std::cerr << "PortAudio stop error: " << Pa_GetErrorText(err) << std::endl;
    }

    err = Pa_CloseStream(stream);
    if (err != paNoError) {
        std::cerr << "PortAudio close error: " << Pa_GetErrorText(err) << std::endl;
    }

    // Terminate PortAudio
    Pa_Terminate();

    std::cout << "Recording complete. Audio saved to output.wav" << std::endl;
    return 0;
}
