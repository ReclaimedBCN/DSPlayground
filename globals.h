#pragma once

#include <atomic>
#include <vector>

// constants
constexpr unsigned int SAMPLERATE = 48000; // should be a sampleRate supported by RTaudio and your soundcard
constexpr unsigned short BUFFERFRAMES = 256; // number of frames per audio callback
constexpr unsigned short RECORDDURATION = 2; // number of seconds to record
constexpr unsigned int RECORDFRAMES = SAMPLERATE * RECORDDURATION; // number of seconds to record
constexpr char DSPPATH[] = "./build/plugins/libdsp.dylib"; // shared library file to load

// globals
struct Globals
{
    unsigned int writeHead = 0; // circular buffer write head
    std::atomic<bool> reloading = 0; // flag to prevent double reloads
    std::vector<float> circularOutput { RECORDFRAMES + BUFFERFRAMES, 0 }; // circular buffer for output frames, sized with 1 extra buffer
};

