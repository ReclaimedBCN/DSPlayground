#pragma once

#include <atomic>
#include <vector>

// constants
constexpr std::size_t SAMPLERATE = 48000; // should be a sampleRate supported by RTaudio and your soundcard
constexpr std::size_t BUFFERFRAMES = 256; // number of frames per audio callback
constexpr std::size_t RECORDDURATION = 3; // number of seconds to record
constexpr std::size_t RECORDFRAMES = SAMPLERATE * RECORDDURATION; // number of frames to record
constexpr char PLUGINSOURCE[] = "plugin.cpp"; // .cpp source file path for plugin
constexpr char PLUGINPATH[] = "./build/plugins/libplugin.dylib"; // shared library file to load
constexpr short BYTETOBITS = 8;
constexpr short RECORDBITDEPTH = 16;

// globals
struct Globals
{
    unsigned int writeHead = 0; // circular buffer write head
    std::atomic<bool> reloading = 0; // flag to prevent double reloads
    std::vector<float> circularOutput = std::vector<float>(RECORDFRAMES + BUFFERFRAMES, 0); // circular buffer for output frames, sized with 1 extra buffer
    std::vector<float> wavWriteFloats = std::vector<float>(RECORDFRAMES, 0);
};

