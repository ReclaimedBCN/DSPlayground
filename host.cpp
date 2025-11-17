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
#include "dsp.h"           // Shared class for DSP parameters

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
// Globals
// -----------------------------------------------------------------------------

DSPModule dsp{}; // global DSPModule object for filling with hot loaded DSPState pointers
std::string dspPath = "build/plugins/libdsp.dylib"; // the shared library file to load
std::atomic<bool> reloading = 0; // flag to prevent double reloads
static_assert (std::atomic<float>::is_always_lock_free); // check float type is lock free

// -----------------------------------------------------------------------------
// Load / Reload the DSP shared library (.dylib) and update the DSPModule struct
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
    dsp.state   = newState;
    dsp.create  = createFn;
    dsp.destroy = destroyFn;
    dsp.process = processFn;

    std::cout << "DSP reloaded successfully\n";
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

// -------------------------------------------------------------------------
// Threaded function for reloading DSP code
    // Function gets called whenver dsp.cpp file is changed
// -------------------------------------------------------------------------
void reloadDspThread()
{
    // rebuild dynamic library
    system("make -C build dsp");
    // reload DSP
    loadDSP(dsp, dspPath);
    // re-enable hot-reloading
    reloading.store(0);
}

// -------------------------------------------------------------------------
// Threaded function for realtime parameter updates
// -------------------------------------------------------------------------
void replThread()
{
    // cast pointer to DSPState object from dsp.cpp to a DSPState object
    DSPState* params = static_cast<DSPState*>(dsp.state);

    std::string param;
    float value;
    std::cout << "REPL ready. Try commands like: <parameter> <value>" << std::endl;

    // start REPL loop
    while (true)
    {
        // assign user input
        std::cout << "> ";
        std::cin >> param >> value;

        // User closed stdin (Ctrl-D)
        if (!std::cin) break;

        if (param == "phase") 
        {
            params->phase.store(value);
            std::cout << "phase set to " << value << "\n";
        }
        else if (param == "freq") 
        {
            params->freq.store(value);
            std::cout << "freq set to " << value << "\n";
        }
        else if (param == "gain") 
        {
            params->gain.store(value);
            std::cout << "gain set to " << value << "\n";
        }
        else if (param == "bypass") 
        {
            params->bypass.store(value != 0);
            std::cout << "bypass set to " << (value != 0) << "\n";
        }
        else if (param == "sampleRate") 
        {
            params->sampleRate.store(static_cast<int>(value));
            std::cout << "sampleRate set to " << value << "\n";
        }
        else std::cout << "unknown parameter\n";
    }
}

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------

int main() 
{
    // Initial load. Fill DSPModule's placeholders with DSPState data from dsp.cpp
    if (!loadDSP(dsp, dspPath)) 
    {
        std::cerr << "Failed initial DSP load\n";
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
    RtAudio::StreamParameters streamParams;
    streamParams.deviceId = dac.getDefaultOutputDevice(); // choose default output
    streamParams.nChannels = 1;                           // mono output

    unsigned int sampleRate = 48000; // must match DSPState.sampleRate
    unsigned int bufferFrames = 256; // number of frames per audio callback

    // -------------------------------
    // Open and start the audio stream
    // -------------------------------
    try
    {
        dac.openStream(&streamParams,         // output stream parameters
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

    std::cout << "Audio stream running\n" << "Edit dsp.cpp to hear changes live\n";

    // -------------------------------------------------------------------------
    // Main loop: Start REPL thread, check for DSP file changes
    // -------------------------------------------------------------------------
    // init / declare variables
    int firstTime = 0;
    std::filesystem::file_time_type lastWriteTime;

    // start REPL in background
    std::thread repl(replThread);
    repl.detach(); // run independently

    while (true) 
    {
        // periodically check if dsp.cpp file changed
            // if so, reload in place without restarting program
        auto currentTime = std::filesystem::last_write_time("dsp.cpp");
        
        // don't trigger rebuild on firstLoop
        if (!firstTime) 
        {
            lastWriteTime = currentTime;
            firstTime = 1;
        }

        // start a new thread when file edited & not currently reloading
        if ((currentTime != lastWriteTime) && !reloading.load())
        {
            // prevent double reloads
            reloading.store(1);
            lastWriteTime = currentTime;

            std::cout << "RELOADING DSP" << std::endl;
            std::thread reload(reloadDspThread);
            // don't block main thread whilst reloading
            reload.detach();
        }
        // Check every 100 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // Safety clean up (usually unreachable)
    dac.closeStream();
}
