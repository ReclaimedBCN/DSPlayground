// -------------------------------
// Hot-Reloadable DSP Code
    // Rebuild with command: make dsp
// -------------------------------

#include <cmath>            // for sinf() and M_PI
#include "dsp.h"

// extern "C" to prevent stripping of symbol names

// ----------------------------------------------------------------------------------------------
// Allocates a new DSPState object on the heap and sets paramters & returns a void* pointer to it
    // Called when the module is first loaded
// ----------------------------------------------------------------------------------------------
extern "C" void* createDSP() 
{
    // initial parameters
    float phase = 0.0f;
    float freq = 220.0f;
    float gain = 1.0f;
    bool bypass = 0;
    int sampleRate = 48000; // sampleRate should match output stream sampleRate

    return new DSPState(phase, freq, gain, bypass, sampleRate);
}

// Frees the memory allocated in createDSP()
    // Called when the module is about to be unloaded (e.g., before hot-reload)
extern "C" void destroyDSP(void* state) 
{
    delete static_cast<DSPState*>(state);
}

// DSP Code: Generates 'numFrames' samples into the 'out' buffer
    // Called once per audio block by the host
extern "C" void processAudio(void* state, float* out, int numFrames) 
{
    // Cast the untyped void* back into a DSPState typed pointer
    auto* params = static_cast<DSPState*>(state);

    // Calculate how much the phase should advance per sample
        // 2π * frequency / sampleRate = radians per sample
    float twoPi = 2.0f * M_PI;
    float phaseInc = twoPi * params->freq.load() / params->sampleRate.load();

    // Generate block of audio samples
    for (int i = 0; i < numFrames; ++i) 
    {
        // Sine wave oscillator @ amplitude 0.2
        out[i] = 0.2f * sinf(params->phase.load());

        // Advance phase for next sample (+= phaseInc)
        params->phase.store(params->phase.load() + phaseInc);
        // Wrap around if phase exceeds 2π (-= 2π)
        if (params->phase.load() > twoPi) params->phase.store(params->phase.load() - twoPi);
    }
}
