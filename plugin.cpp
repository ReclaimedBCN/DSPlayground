// -------------------------------
// Hot-Reloadable DSP Code
    // Rebuild with command: make plugin
// -------------------------------

#include <cmath>            // for sinf() and M_PI

#include "globals.h"
#include "plugin.h"

// extern "C" to prevent stripping of symbol names

// ----------------------------------------------------------------------------------------------
// Allocates a new PluginState object on the heap and sets paramters & returns a void* pointer to it
    // Called when the module is first loaded
// ----------------------------------------------------------------------------------------------
extern "C" void* createPlugin() 
{
    // initial parameters
    float phase = 0.0f;
    float freq = 220.0f;
    float gain = 0.1f;
    bool bypass = 0;
    int sampleRate = SAMPLERATE; // sampleRate should match output stream sampleRate

    return new PluginState(phase, freq, gain, bypass, sampleRate);
}

// Frees the memory allocated in createPlugin()
    // Called when the module is about to be unloaded (e.g., before hot-reload)
extern "C" void destroyPlugin(void* state) { delete static_cast<PluginState*>(state); }

// DSP Code: Generates 'numFrames' samples into the 'out' buffer
    // Called once per audio block by the host
extern "C" void processPlugin(void* state, float* out, int numFrames) 
{
    // Cast the untyped void* back into a PluginState typed pointer
    auto* params = static_cast<PluginState*>(state);

    // assign atomics to local variables (for easier syntax within DSP calculations)
    float phase = params->phase.load();
    float freq = params->freq.load();
    float gain = params->gain.load();
    float bypass = params->bypass.load();
    int sampleRate = params->sampleRate.load();

    // Calculate how much the phase should advance per sample
        // 2π * frequency / sampleRate = radians per sample
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
    params->phase.store(phase);
}
