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
            _uiParams = static_cast<UiParams*>(uiParamsPoint); 
        }

        void process(float* out, int numFrames)
        {
            // assign ui's atomics to local variables for easier syntax within DSP calculations
            float bypass = 0;
            if (_uiParams)
            {
                bypass = _uiParams->bypass.load();
            }

            // optional parameter smoothing
            constexpr float smoothing = 0.01f;
            float targetFreq = _freq;
            float targetGain = _gain;
            if (_uiParams)
            {
                targetFreq = _uiParams->freq;
                targetGain = _uiParams->gain;
            }

            // phase increment per sample
            float twoPi = 2.0f * M_PI;
            float phaseInc = twoPi * _freq / _sampleRate;

            // generate block of audio samples
            for (int i = 0; i < numFrames; ++i) 
            {
                // optional parameter smoothing
                _freq += smoothing * (targetFreq - _freq);
                _gain += smoothing * (targetGain - _gain);

                // sine wave oscillator @ amplitude 0.2
                out[i] = !bypass * _gain * sinf(_phase);
                // advance phase for next sample
                _phase += phaseInc;
                // wrap around if phase exceeds 2π
                if (_phase > twoPi) _phase -= twoPi;
            }
            // store any changed ui params
            if (_uiParams) 
            { 
                // e.g. uiParams->bypass.store(bypass); 
            }
        }
    private:
        int _sampleRate = SAMPLERATE; // sampleRate should match output stream sampleRate
        float _phase = 0.f;
        float _freq = 220.f;
        float _gain = 0.5f;

        UiParams* _uiParams = nullptr;
};

