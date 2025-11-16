/*
Hot-Reloadable DSP Code
    Rebuild with command: make dsp
*/

#include <cmath>            // for sinf() and M_PI

// Struct to hold all info about the per-instance DSP state
    // Host never looks inside — just stores a void* to it
struct DSPState {
    float phase = 0.0f;             // current oscillator phase in radians
    float freq  = 220.0f;           // oscillator frequency in Hz
    float sampleRate = 48000.0f;    // assumed sample rate (should match host)
};

// extern "C" to prevent stripping of symbol names

// Allocates a new DSPState object on the heap and returns a void* pointer to it
    // Called when the module is first loaded
extern "C" void* createDSP() {
    return new DSPState();
}

// Frees the memory allocated in createDSP()
    // Called when the module is about to be unloaded (e.g., before hot-reload)
extern "C" void destroyDSP(void* state) {
    delete static_cast<DSPState*>(state);
}

// DSP Code: Generates 'numFrames' samples into the 'out' buffer
    // Called once per audio block by the host
extern "C" void processAudio(void* state, float* out, int numFrames) {
    // Cast the untyped void* back into a DSPState typed pointer
    auto* s = static_cast<DSPState*>(state);

    // Calculate how much the phase should advance per sample
        // 2π * frequency / sampleRate = radians per sample
    float twoPi = 2.0f * M_PI;
    float phaseInc = twoPi * s->freq / s->sampleRate;

    // Generate block of audio samples
    for (int i = 0; i < numFrames; ++i) 
    {
        // Sine wave oscillator @ amplitude 0.2
        out[i] = 0.2f * sinf(s->phase);

        // Advance phase for next sample
        s->phase += phaseInc;
        // Wrap around if phase exceeds 2π
        if (s->phase > twoPi) s->phase -= twoPi;
    }
}
