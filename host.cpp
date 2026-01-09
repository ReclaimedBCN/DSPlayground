// -----------------------------------------------------------------------------
// RtAudio Host with DSP Plugin Hot-Reload:
    // flow: inital load, audio setup, dynamic reload loop, cleanup
// -----------------------------------------------------------------------------
#include <iostream>
#include <thread>
#include <filesystem>
#include <chrono>
#include <dlfcn.h>         // for dlopen, dlsym, dlclose

#include "RtAudio.h"       // RtAudio for cross-platform audio I/O

#include "globals.h"
#include "plugin.h"           // Shared class for DSP parameters
#include "wavParser.h"

static_assert (std::atomic<float>::is_always_lock_free); // check float type is lock free

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
Globals globals;
unsigned int rtBufferFrames = BUFFERFRAMES; // assign constant to mutable as RtAudio will change value if unsupported by system

// -----------------------------------------------------------------------------
// Struct to hold function pointers and state for hot loaded data from plugin.cpp
// -----------------------------------------------------------------------------
struct PluginModule 
{
    void* handle = nullptr;                 // dynamic library handle returned by dlopen()
    void* state = nullptr;                  // pointer to DSPState instance created by DSP module
    void* (*create)();                      // function pointer: createDSP()
    void (*destroy)(void*);                 // function pointer: destroyDSP()
    void (*process)(void*, float*, int);    // function pointer: processAudio() + floatOut + numFrames
};
PluginModule plugin{}; // for filling with hot loaded PluginState pointers

// -----------------------------------------------------------------------------
// Load / Reload the Plugin shared library (.dylib) and update the PluginModule struct
// -----------------------------------------------------------------------------
bool loadPlugin(PluginModule& plugin) 
{
    // Try to open the shared library file
    void* handle = dlopen(PLUGINPATH, RTLD_NOW);
    // null pointer catch
    if (!handle) 
    {
        std::cerr << "Failed to load Plugin: " << dlerror() << "\n";
        return false;
    }

    // Resolve the symbols (function names) expected from plugin.cpp
        // strings and types must match what's declared in plugin.h and implemented in plugin.cpp
    auto createFn  = (void* (*)())dlsym(handle, "createPlugin");
    auto destroyFn = (void (*)(void*))dlsym(handle, "destroyPlugin");
    auto processFn = (void (*)(void*, float*, int))dlsym(handle, "processPlugin");

    // Check all functions were found
    if (!createFn || !destroyFn || !processFn) 
    {
        std::cerr << "Invalid Plugin symbols: " << dlerror() << "\n";
        dlclose(handle);
        return false;
    }

    // If plugin is already loaded, free it's memory before creating a new PluginState object
    if (plugin.destroy && plugin.state) plugin.destroy(plugin.state);
    // Create a new PluginState instance with the new module
    void* newState = createFn();

    // Unload the previous shared library (if any) before replacing it
    if (plugin.handle) dlclose(plugin.handle);

    // Store the new handles and function pointers.
    plugin.handle  = handle;
    plugin.state   = newState;
    plugin.create  = createFn;
    plugin.destroy = destroyFn;
    plugin.process = processFn;

    std::cout << "Plugin reloaded successfully" << std::endl;
    std::cout << "REPL ready. Try commands like: <parameter> <value>" << std::endl;
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
        // cast userData pointer back to a PluginModule object pointer, same for globals
        PluginModule* plugin = static_cast<PluginModule*>(userData);

        // If the plugin is loaded and valid, generate samples via processAudio function in plugin.cpp
        if (plugin->process && plugin->state) plugin->process(plugin->state, static_cast<float*>(outBuffer), numFrames);
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
void uiThread()
{
    std::string param;
    float value;

    if (globals.reloading.load() == 0) std::cout << "REPL ready. Try commands like: <parameter> <value>" << std::endl;

    // start REPL loop
    while (true) 
    {
        // assign user input
        std::cout << "> ";
        std::cin >> param >> value;
        // User closed stdin (Ctrl-D)
        if (!std::cin) break;

        // If plugin reloaded, params pointer no-longer valid
        if (globals.reloading.load() == 1) 
        {   
            std::cout << "Plugin reloading, please wait.." << std::endl;
            break;
        }
        // cast plugin.cpp's pointer to a PluginState object
        PluginState* params = static_cast<PluginState*>(plugin.state);

        if (param == "phase") 
        {
            if (params) params->phase.store(value);
            std::cout << "phase set to " << value << "\n";
        }
        else if (param == "freq") 
        {
            if (params) params->freq.store(value);
            std::cout << "freq set to " << value << "\n";
        }
        else if (param == "gain") 
        {
            if (params) params->gain.store(value);
            std::cout << "gain set to " << value << "\n";
        }
        else if (param == "bypass") 
        {
            if (params) params->bypass.store(value != 0);
            std::cout << "bypass set to " << (value != 0) << "\n";
        }
        else if (param == "sampleRate") 
        {
            if (params) params->sampleRate.store(static_cast<int>(value));
            std::cout << "sampleRate set to " << value << "\n";
        }
        else if (param == "rec")
        {
            std::thread wavWrite(wavWriteThread);
            wavWrite.detach(); // run independently
        }
        else std::cout << "unknown parameter\n";
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    uiThread();
}

// -------------------------------------------------------------------------
// Asynchronous function for reloading plugin code
    // Called whenver plugin.cpp file is changed
// -------------------------------------------------------------------------
void reloadPluginThread()
{
    // rebuild dynamic library
    system("make -C build plugin");
    loadPlugin(plugin); // reload plugin
    globals.reloading.store(0); // re-enable hot-reloading
}

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------

int main() 
{
    // Initial load, fill PluginModule's placeholders with data from plugin.cpp
    if (!loadPlugin(plugin)) 
    {
        std::cerr << "Failed initial plugin load\n";
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
                       &plugin);           // userData to pass to callback
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

    std::cout << "Audio stream running\n" << "Edit plugin.cpp to hear changes live\n";

    // -------------------------------------------------------------------------
    // Main loop: Start UI thread, check for plugin file changes
    // -------------------------------------------------------------------------
    // setup
    int firstTime = 0;
    std::filesystem::file_time_type lastWriteTime;

    // start UI (and potentially wavWriter) in background
    std::thread ui(uiThread);
    ui.detach(); // run independently

    while (true) 
    {
        // periodically check if plugin.cpp file changed
            // if so, reload in place without restarting program
        auto currentTime = std::filesystem::last_write_time(PLUGINSOURCE);
        
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

            std::cout << "RELOADING PLUGIN" << std::endl;
            std::thread reload(reloadPluginThread);
            // don't block main thread whilst reloading
            reload.detach();
        }
        // Check every 300 ms
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
    }

    // Safety clean up (usually unreachable)
    dac.closeStream();
}
