// -------------------------------
// Shared class for DSP Parameters
// -------------------------------
#pragma once

#include <atomic>
#include <cmath> // for sinf() and M_PI
#include "globals.h"

// Struct to hold all info about the per-instance DSP state
class PluginState
{
    public:
        void process(float* out, int numFrames)
        {
            // assign atomics to local variables for easier syntax within DSP calculations
            float phase = this->_phase.load();
            float freq = this->_freq.load();
            float gain = this->_gain.load();
            float bypass = this->_bypass.load();
            int sampleRate = this->_sampleRate.load();

            // How much the phase should advance per sample
            float twoPi = 2.0f * M_PI;
            float phaseInc = twoPi * freq / sampleRate;

            // Generate block of audio samples
            for (int i = 0; i < numFrames; ++i) 
            {
                // Sine wave oscillator @ amplitude 0.2
                out[i] = !bypass * gain * sinf(phase);
                // Advance phase for next sample
                phase += phaseInc;
                // Wrap around if phase exceeds 2π
                if (phase > twoPi) phase -= twoPi;
            }
            // store any locally changed params
            this->_phase.store(phase);
        }
    private:
        std::atomic<float> _freq = 220.f;
        std::atomic<float> _gain = 0.1f;
        std::atomic<float> _phase = 0.f;
        std::atomic<bool>  _bypass = 0;
        std::atomic<int> _sampleRate = SAMPLERATE; // sampleRate should match output stream sampleRate
};
