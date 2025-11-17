// -------------------------------
// Shared class for DSP Parameters
// -------------------------------

#pragma once

#include <atomic>

// Struct to hold all info about the per-instance DSP state
    // Host never looks inside — just stores a void* to it
class DSPState
{
    public:
        std::atomic<float> phase;
        std::atomic<float> freq;
        std::atomic<float> gain;
        std::atomic<bool>  bypass;
        std::atomic<int> sampleRate;

        DSPState(float phaseInit, float freqInit, float gainInit, bool bypassInit, int sampleRateInit): 
            phase(phaseInit),
            freq(freqInit),
            gain(gainInit),
            bypass(bypassInit),
            sampleRate(sampleRateInit)
        {
        }
    private:
};
