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
        PluginState(void* uiParamsPoint) 
        { 
            uiParams = static_cast<UiParams*>(uiParamsPoint); 
        }

        void process(float* out, int numFrames)
        {
            // assign atomics to local variables for easier syntax within DSP calculations
            float phase = this->aPhase.load();
            float freq = this->aFreq.load();
            float gain = this->aGain.load();
            float bypass = this->aBypass.load();
            int sampleRate = this->aSampleRate.load();

            // optional parameter smoothing
            constexpr float smoothing = 0.01f;
            float targetFreq = freq;
            float targetGain = gain;
            if (uiParams) // if valid, assign uiParams to smoothing targets
            {
                targetFreq = uiParams->freq;
                targetGain = uiParams->gain;
                bypass = uiParams->bypass;
            }

            // phase increment per sample
            float twoPi = 2.0f * M_PI;
            float phaseInc = twoPi * freq / sampleRate;

            // generate block of audio samples
            for (int i = 0; i < numFrames; ++i) 
            {
                // optional parameter smoothing
                freq += smoothing * (targetFreq - freq);
                gain += smoothing * (targetGain - gain);

                // sine wave oscillator @ amplitude 0.2
                out[i] = !bypass * gain * sinf(phase);
                // advance phase for next sample
                phase += phaseInc;
                // wrap around if phase exceeds 2π
                if (phase > twoPi) phase -= twoPi;
            }
            // store any locally changed params
            this->aFreq.store(freq);
            this->aPhase.store(phase);
            this->aBypass.store(phase);
        }
    private:
        std::atomic<float> aFreq = 220.f;
        std::atomic<float> aGain = 0.1f;
        std::atomic<float> aPhase = 0.f;
        std::atomic<bool>  aBypass = 0;
        std::atomic<int> aSampleRate = SAMPLERATE; // sampleRate should match output stream sampleRate

        UiParams* uiParams = nullptr;
};

