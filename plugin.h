// Copyright 2026 Reclaimed BCN. All rights reserved.
// Use of this source code is governed by the license found in the LICENSE file.

#pragma once

#include <atomic>
#include <cmath> // for sinf() and M_PI
#include "globals.h"

// -------------------------------------------
// Shared class for holding plugin's DSP State 
// -------------------------------------------
class PluginState
{
    public:
        PluginState(void* uiParamsPtr) 
        { 
            _uiParams = static_cast<UiParams*>(uiParamsPtr); 
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
            constexpr float smoothing = 0.005f;
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

            // generate interleaved stereo block of samples
            for (int i=0; i<numFrames; ++i) 
            {
                // optional parameter smoothing
                _freq += smoothing * (targetFreq - _freq);
                _gain += smoothing * (targetGain - _gain);

                // sine wave oscillator out
                float output = !bypass * _gain * sinf(_phase);
                out[2*i+0] = output; // Left channel
                out[2*i+1] = output; // Right channel

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
        int _sampleRate = SAMPLERATE;
        float _phase = 0.f;
        float _freq = 220.f;
        float _gain = 0.5f;

        UiParams* _uiParams = nullptr;
};

