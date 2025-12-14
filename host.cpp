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

#include "globals.h"
#include "dsp.h"           // Shared class for DSP parameters
#include "wavParser.h"

static_assert (std::atomic<float>::is_always_lock_free); // check float type is lock free

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
Globals globals;
unsigned int rtBufferFrames = BUFFERFRAMES; // assign constant to mutable as RtAudio will change value if unsupported by system

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
DSPModule dsp{}; // for filling with hot loaded DSPState pointers

// -----------------------------------------------------------------------------
// Load / Reload the DSP shared library (.dylib) and update the DSPModule struct
// -----------------------------------------------------------------------------
bool loadDSP(DSPModule& dsp) 
{
    // Try to open the shared library file
    void* handle = dlopen(DSPPATH, RTLD_NOW);
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

    // Check all functions were found
    if (!createFn || !destroyFn || !processFn) 
    {
        std::cerr << "Invalid DSP symbols: " << dlerror() << "\n";
        dlclose(handle);
        return false;
    }

    // If DSP is already loaded, free it's memory before creating a new DSPState object
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
// RtAudio callback function
    // Called by RtAudio whenever it needs more audio samples
// -------------------------------------------------------------------------
int callback(void* outBuffer, void*, unsigned int numFrames, double, RtAudioStreamStatus, void* userData)
{
    if(userData) // null pointer check
    {
        // userData is a pointer passed when opening stream in try block below
        // cast userData pointer back to a DSPModule object pointer, same for globals
        DSPModule* dsp = static_cast<DSPModule*>(userData);
        // Globals* globals = static_cast<Globals*>(dsp->globals);

        // If the DSP is loaded and valid, generate samples via processAudio function in dsp.cpp
        if (dsp->process && dsp->state) dsp->process(dsp->state, static_cast<float*>(outBuffer), numFrames);
        // Otherwise, output silence (avoid noise on error)
        else std::fill_n(static_cast<float*>(outBuffer), numFrames, 0.0f);

        // write ouput buffer to circular buffer for extra functions
        for (int i=0; i<numFrames; i++) 
        {
            globals.circularOutput[globals.writeHead] = static_cast<float*>(outBuffer)[i];
            globals.writeHead = (globals.writeHead + 1) % globals.circularOutput.size();
        }
    }
    return 0; // exit code so RtAudio continues streaming
}

// -------------------------------------------------------------------------
// Asynchronous function for reloading DSP code
    // Called whenver dsp.cpp file is changed
// -------------------------------------------------------------------------
void reloadDspThread()
{
    // rebuild dynamic library
    system("make -C build dsp");
    loadDSP(dsp); // reload DSP
    globals.reloading.store(0); // re-enable hot-reloading
}

// -------------------------------------------------------------------------
// Asynchronous function for exporting a .wav file
// -------------------------------------------------------------------------
void wavWriteThread()
{
    // std::cout << "wait for it.. " << std::endl;
    // std::this_thread::sleep_for(std::chrono::milliseconds(6000));
    writeWav(globals);
}

// -------------------------------------------------------------------------
// Asynchronous function for realtime parameter updates
// -------------------------------------------------------------------------
void replThread()
{
    // cast dsp.cpp's pointer to a DSPState object
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
        else if (param == "rec")
        {
            std::thread wavWrite(wavWriteThread);
            wavWrite.detach(); // run independently
        }
        else std::cout << "unknown parameter\n";
    }
}

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------

int main() 
{
    // Initial load, fill DSPModule's placeholders with data from dsp.cpp
    if (!loadDSP(dsp)) 
    {
        std::cerr << "Failed initial DSP load\n";
        return 1;
    }

    // -------------------------------
    // Setup RtAudio output stream
    // -------------------------------
    RtAudio dac; // RtAudio output DAC for interfacing with sound card

    if (dac.getDeviceCount() < 1) 
    {
        std::cerr << "No audio devices found!\n";
        return 1;
    }

    // Configure output stream parameters
    RtAudio::StreamParameters streamParams;
    streamParams.deviceId = dac.getDefaultOutputDevice(); // choose default output
    streamParams.nChannels = 1;                           // mono output


    // -------------------------------
    // Open and start the audio stream
    // -------------------------------
    try
    {
        dac.openStream(&streamParams,   // output stream parameters
                       nullptr,         // no input stream
                       RTAUDIO_FLOAT32, // sample format
                       SAMPLERATE,
                       &rtBufferFrames,   // number of sample frames per callback
                       callback,        // callback function name
                       &dsp);           // userData to pass to callback
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
    // setup
    int firstTime = 0;
    std::filesystem::file_time_type lastWriteTime;

    // start REPL (and potentially wavWriter) in background
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
        if ((currentTime != lastWriteTime) && !globals.reloading.load())
        {
            // prevent double reloads
            globals.reloading.store(1);
            lastWriteTime = currentTime;

            std::cout << "RELOADING DSP" << std::endl;
            std::thread reload(reloadDspThread);
            // don't block main thread whilst reloading
            reload.detach();
        }
        // Check every 300 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // Safety clean up (usually unreachable)
    dac.closeStream();
}
