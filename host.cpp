// -----------------------------------------------------------------------------
// RtAudio Host with DSP Hot-Reload:
    // flow: inital load, audio setup, dynamic reload loop, cleanup
// -----------------------------------------------------------------------------

#include <iostream>
#include <thread>
#include <filesystem>
#include <chrono>
#include <dlfcn.h>         // for dlopen, dlsym, dlclose
#include "RtAudio.h"       // RtAudio for cross-platform audio I/O

// -----------------------------------------------------------------------------
// Struct to hold function pointers and state for hot loaded data from dsp.cpp
// -----------------------------------------------------------------------------
struct DSPModule 
{
    void* handle = nullptr;                 // dynamic library handle returned by dlopen()
    void* state = nullptr;                  // pointer to DSPState instance created by DSP module
    void* (*create)();                      // function pointer: createDSP()
    void (*destroy)(void*);                 // function pointer: destroyDSP()
    void (*process)(void*, float*, int);    // function pointer: processAudio() + floatOut + numFrames
};

// -----------------------------------------------------------------------------
// Load / Reload the DSP shared library (.so) and update the DSPModule struct
// -----------------------------------------------------------------------------
bool loadDSP(DSPModule& dsp, const std::string& path) 
{
    // Try to open the shared library file
    void* handle = dlopen(path.c_str(), RTLD_NOW);
        // null pointer catch
    if (!handle) 
    {
        std::cerr << "Failed to load DSP: " << dlerror() << "\n";
        return false;
    }

    // Resolve the symbols (function names) expected from dsp.cpp
        // strings and types must match what's declared in dsp_interface.h and implemented in dsp.cpp
    auto createFn  = (void* (*)())dlsym(handle, "createDSP");
    auto destroyFn = (void (*)(void*))dlsym(handle, "destroyDSP");
    auto processFn = (void (*)(void*, float*, int))dlsym(handle, "processAudio");

    // Validate all required functions were found
    if (!createFn || !destroyFn || !processFn) 
    {
        std::cerr << "Invalid DSP symbols: " << dlerror() << "\n";
        dlclose(handle);
        return false;
    }

    // If a DSP is already loaded, free it's memory before creating a new DSPState object
    if (dsp.destroy && dsp.state) dsp.destroy(dsp.state);
    // Create a new DSP state instance with the new module
    void* newState = createFn();

    // Unload the previous shared library (if any) before replacing it
    if (dsp.handle) dlclose(dsp.handle);

    // Store the new handles and function pointers.
    dsp.handle  = handle;
    dsp.create  = createFn;
    dsp.destroy = destroyFn;
    dsp.process = processFn;
    dsp.state   = newState;

    std::cout << "DSP reloaded successfully.\n";
    return true;
}

// -------------------------------------------------------------------------
// Define RtAudio callback function
    // Function gets called by RtAudio whenever it needs more audio samples.
// -------------------------------------------------------------------------
int callback(void* outputBuffer, void*, unsigned int nFrames, double, RtAudioStreamStatus, void* userData)
{
    // userData is a pointer passed when opening stream in try block below
    // cast userData pointer back to a DSPModule object pointer
    DSPModule* dsp = static_cast<DSPModule*>(userData);

    // If the DSP is loaded and valid, generate samples via processAudio function in dsp.cpp
    if (dsp->process && dsp->state) dsp->process(dsp->state, static_cast<float*>(outputBuffer), nFrames);
    // Otherwise, output silence (avoid noise on error)
    else std::fill_n(static_cast<float*>(outputBuffer), nFrames, 0.0f);

    return 0; // return 0 so RtAudio continues streaming
}

// -----------------------------------------------------------------------------
// Main entry point
// -----------------------------------------------------------------------------

int main() 
{
    std::string dspPath = "plugins/libdsp.dylib"; // the shared library file to load
    auto lastWrite = std::filesystem::last_write_time(dspPath);

    // create a DSPModule object for filling with hot loaded DSPState pointers
    DSPModule dsp{};

    // Initial load. Fill DSPModule's placeholders with DSPState data from dsp.cpp
    if (!loadDSP(dsp, dspPath)) 
    {
        std::cerr << "Failed initial DSP load.\n";
        return 1;
    }

    // -------------------------------
    // Setup RtAudio output stream
    // -------------------------------
    RtAudio dac; // RtAudio output DAC object for interfacing with sound card

    if (dac.getDeviceCount() < 1) 
    {
        std::cerr << "No audio devices found!\n";
        return 1;
    }

    // Configure output stream parameters
    RtAudio::StreamParameters params;
    params.deviceId = dac.getDefaultOutputDevice(); // choose default output
    params.nChannels = 1;                           // mono output

    unsigned int sampleRate = 48000; // must match DSPState.sampleRate
    unsigned int bufferFrames = 256; // number of frames per audio callback

    // -------------------------------
    // Open and start the audio stream
    // -------------------------------
    try
    {
        dac.openStream(&params,         // output stream parameters
                       nullptr,         // no input stream
                       RTAUDIO_FLOAT32, // sample format
                       sampleRate,
                       &bufferFrames,   // sample frames per callback
                       callback,        // audio callback
                       &dsp);           // userData (passed to callback)
        dac.startStream();
    }
    catch (RtAudioErrorType& errCode)
    {
        std::cout << dac.getErrorText() << std::endl;
        if (errCode == 0) std::cout << "No error" << std::endl;
        else if (errCode == 1) std::cout << "Non-critical error" << std::endl;
        else if (errCode == 2) std::cout << "UNspecified error type" << std::endl;
        else if (errCode == 3) std::cout << "No devices found" << std::endl;
        else if (errCode == 4) std::cout << "Invalid device ID was specified" << std::endl;
        else if (errCode == 5) std::cout << "Device in use was disconnected" << std::endl;
        else if (errCode == 6) std::cout << "Error occurred during memeory allocation" << std::endl;
        else if (errCode == 7) std::cout << "Invalid parameter was specified to a fucntion" << std::endl;
        else if (errCode == 8) std::cout << "Function was called incoorectly" << std::endl;
        else if (errCode == 9) std::cout << "System driver error occurred" << std::endl;
        else if (errCode == 10) std::cout << "System error occurred" << std::endl;
        else if (errCode == 11) std::cout << "Thread error ocurred" << std::endl;
        return 1;
    }

    std::cout << "Audio stream running.\n"
              << "Edit and rebuild dsp.cpp to hear changes live.\n";

    // -------------------------------------------------------------------------
    // Main loop: Check for DSP file changes
    // -------------------------------------------------------------------------
    while (true) 
    {
        // periodically check if DSP shared library file changed
            // if so, reload in place without restarting program
        auto newTime = std::filesystem::last_write_time(dspPath);
        if (newTime != lastWrite) 
        {
            lastWrite = newTime;
            loadDSP(dsp, dspPath);
        }

        // Check every 100 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    // Safety clean up (usually unreachable)
    dac.closeStream();
}
