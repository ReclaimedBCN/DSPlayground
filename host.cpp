// Copyright 2026 Reclaimed BCN. All rights reserved.
// Use of this source code is governed by the license found in the LICENSE file.

#include <cstddef>
#include <iostream>
#include <thread>
#include <filesystem>
#include <chrono>
#include <dlfcn.h>

#include "RtAudio.h"

#include "globals.h"
#include "wavEncoder.h"
#include "ui.h"

static_assert (std::atomic<float>::is_always_lock_free); // check float type is lock free

// Globals
Globals globals;
unsigned int rtBufferFrames = BUFFERFRAMES; // assign constant to mutable as RtAudio will change value if unsupported by system
LogBuffer logBuff; // circular buffer for logging standard output
PluginModule plugin{}; // for filling with hot loaded PluginState pointers
UiParams uiParams;

// -----------------------------------------------------------------------------
// Load / Reload the Plugin shared library (.dylib) and update the PluginModule struct
// -----------------------------------------------------------------------------
bool loadPlugin(PluginModule& plugin) 
{
    // Try to open the shared library file
    void* handle = dlopen(PLUGINPATH, RTLD_NOW);
    if (!handle) // null ptr check
    {
        std::cerr << "Failed to load Plugin: " << dlerror() << "\n";
        return false;
    }

    // Resolve the symbols (function names) expected from plugin.cpp
        // strings and types must match what's declared in plugin.h and implemented in plugin.cpp
    auto createFn  = (void* (*)(void*))dlsym(handle, "createPlugin");
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
    if (plugin.destroy && plugin.state) 
    {
        plugin.destroy(plugin.state);
    }
    // Create a new PluginState instance with the new module
    void* newState = createFn(&uiParams);

    // Unload the previous shared library (if any) before replacing it
    if (plugin.handle) dlclose(plugin.handle);

    // Store the new handles and function pointers.
    plugin.handle  = handle;
    plugin.state   = newState;
    plugin.create  = createFn;
    plugin.destroy = destroyFn;
    plugin.process = processFn;

    logBuff.setNewLine("Plugin reloaded successfully");
    return true;
}

// -------------------------------------------------------------------------
// RtAudio callback, called by RtAudio whenever it needs more audio samples
// -------------------------------------------------------------------------
int callback(void* outBuffer, void*, unsigned int numFrames, double, RtAudioStreamStatus, void* userData)
{
    if(userData) // null pointer check
    {
        // cast userData pointer back to a PluginModule object pointer, same for globals
        PluginModule* plugin = static_cast<PluginModule*>(userData);

        // If the plugin is loaded and valid, generate samples via processAudio function in plugin.cpp
        if (plugin->process && plugin->state) plugin->process(plugin->state, static_cast<float*>(outBuffer), numFrames);
        // Otherwise, output silence (avoid noise on error)
        else std::fill_n(static_cast<float*>(outBuffer), numFrames, 0.0f);

        // write ouput buffer to circular buffer for extra functions
        for (int i=0; i<numFrames; i++) 
        {
            int writeHead = globals.writeHead.load();
            globals.circularOutput[writeHead] = static_cast<float*>(outBuffer)[i];
            int wrapped = (writeHead + 1) % globals.circularOutput.size();
            globals.writeHead.store(wrapped);
        }
    }
    return 0; // exit code so RtAudio continues streaming
}

// -------------------------------------------------------------------------
// Async function for exporting a .wav file
// -------------------------------------------------------------------------
void wavWriteThread() { writeWav(globals, logBuff); }

// -------------------------------------------------------------------------
// Async function for realtime parameter updates & visualisers
// -------------------------------------------------------------------------
void uiThread() { drawUi(logBuff, globals, uiParams); }

// -------------------------------------------------------------------------
// Async function for reloading plugin code when plugin.h file is changed
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

    // Setup RtAudio output stream
    RtAudio dac;
    if (dac.getDeviceCount() < 1) 
    {
        std::cerr << "No audio devices found!\n";
        return 1;
    }

    // Configure output stream parameters
    RtAudio::StreamParameters streamParams;
    streamParams.deviceId = dac.getDefaultOutputDevice(); // default output
    streamParams.nChannels = 2;                           // stereo output

    // start UI (and potentially wavWriter) in background
    std::thread ui(uiThread);
    ui.detach(); // run independently

    // Open and start the audio stream
    try
    {
        dac.openStream(&streamParams,       // output stream parameters
                       nullptr,             // no input stream
                       RTAUDIO_FLOAT32,     // sample format
                       SAMPLERATE,
                       &rtBufferFrames,     // number of sample frames per callback
                       callback,            // callback function name
                       &plugin);            // userData to pass to callback
        dac.startStream();
    }
    catch (RtAudioErrorType& errCode)
    {
        logBuff.setNewLine(dac.getErrorText());
        if (errCode == 0) logBuff.setNewLine("No error");
        else if (errCode == 1) logBuff.setNewLine("Non-critical error");
        else if (errCode == 2) logBuff.setNewLine("Unspecified error type");
        else if (errCode == 3) logBuff.setNewLine("No devices found");
        else if (errCode == 4) logBuff.setNewLine("Invalid device ID was specified");
        else if (errCode == 5) logBuff.setNewLine("Device in use was disconnected");
        else if (errCode == 6) logBuff.setNewLine("Error occurred during memeory allocation");
        else if (errCode == 7) logBuff.setNewLine("Invalid parameter was specified to a fucntion");
        else if (errCode == 8) logBuff.setNewLine("Function was called incoorectly");
        else if (errCode == 9) logBuff.setNewLine("System driver error occurred");
        else if (errCode == 10) logBuff.setNewLine("System error occurred");
        else if (errCode == 11) logBuff.setNewLine("Thread error ocurred");
        return 1;
    }
    logBuff.setNewLine("Audio stream running");
    logBuff.setNewLine("Edit plugin.h to hear changes live");

    // if plugin changes, reload in place without restarting program
    int firstTime = 0;
    std::filesystem::file_time_type lastWriteTime;

    // Periodically check plugin.h file for changes
    while (true) 
    {
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

            logBuff.setNewLine("RELOADING PLUGIN");
            std::thread reload(reloadPluginThread);
            reload.detach(); // don't block main thread whilst reloading
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    // Safety clean up (usually unreachable)
    dac.closeStream();
}
