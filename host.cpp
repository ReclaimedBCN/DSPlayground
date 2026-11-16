// Copyright 2026 Reclaimed BCN. All rights reserved.
// Use of this source code is governed by the license found in the LICENSE file.

#include <cstddef>
#include <iostream>
#include <thread>
#include <filesystem>
#include <chrono>
#include <dlfcn.h>
#include <string>

#include "RtAudio.h"

#include "globals.h"
#include "wavEncoder.h"
#include "ui.h"

static_assert(std::atomic<float>::is_always_lock_free, "your device doesn't support atomic floats");

// globals
Globals globals;
unsigned int rtBufferFrames = BUFFERFRAMES; // mutable as RtAudio will change value if unsupported by system
LogBuffer logBuff;
PluginModule plugin{}; // fill with hot loaded PluginState pointers
UiParams uiParams;

// OS detection for correct file format
std::string sharedLibraryName(const std::string& baseName)
{
#if defined(_WIN32)
    return baseName + ".dll";
#elif defined(__APPLE__) && defined(__MACH__)
    return "lib" + baseName + ".dylib";
#elif defined(__linux__)
    return "lib" + baseName + ".so";
#else
    #error Unsupported platform
#endif
}

// ----------------------------------------------------------------------------------------------
// Load / Reload the Plugin shared library (.dylib/.so/.dll) and update the PluginModule struct
// ----------------------------------------------------------------------------------------------
bool loadPlugin(PluginModule& plugin) 
{
    std::string pluginPath = "./build/plugins/" + sharedLibraryName("plugin");
    // try to open the shared library file
    void* handle = dlopen(pluginPath.c_str(), RTLD_NOW);
    if (!handle)
    {
        std::cerr << "Failed to load Plugin: " << dlerror() << "\n";
        return false;
    }

    // resolve function names expected from plugin.cpp
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

    // if plugin is already loaded, free it's memory before creating a new PluginState object
    if (plugin.destroy && plugin.state) plugin.destroy(plugin.state);
    void* newState = createFn(&uiParams);
    // unload the previous shared library (if any) before replacing it
    if (plugin.handle) dlclose(plugin.handle);

    // store the new handles and function pointers
    plugin.handle  = handle;
    plugin.state   = newState;
    plugin.create  = createFn;
    plugin.destroy = destroyFn;
    plugin.process = processFn;

    logBuff.setNewLine("Plugin reloaded successfully");
    return true;
}

// -------------------------------------------------------------------------
// RtAudio callback, called by RtAudio when it needs more samples
// -------------------------------------------------------------------------
int callback(void* outBuffer, void*, unsigned int numFrames, double, RtAudioStreamStatus, void* userData)
{
    if(userData)
    {
        PluginModule* plugin = static_cast<PluginModule*>(userData);
        float* outFloats = static_cast<float*>(outBuffer);

        // if plugin is loaded and valid, generate samples
        if (plugin->process && plugin->state) plugin->process(plugin->state, outFloats, numFrames);
        else std::fill_n(outFloats, numFrames, 0.0f); // ouput silence

        // write ouput to circular buffer for extra functions
        for (int i=0; i<numFrames; i++) 
        {
            int writeHead = globals.writeHead.load();
            globals.circularOutput[writeHead] = outFloats[i];
            int wrapped = (writeHead + 1) % globals.circularOutput.size();
            globals.writeHead.store(wrapped);
        }
    }
    return 0; // exit code so RtAudio continues streaming
}

// Async functions
void wavWriteThread() { writeWav(globals, logBuff); }
void uiThread() { drawUi(logBuff, globals, uiParams); }
void reloadPluginThread()
{
    // rebuild dynamic library
    system("make -C build plugin");
    loadPlugin(plugin);
    globals.reloading.store(0); // re-enable hot-reloading
}

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------
int main() 
{
    // initial load, fill PluginModule's placeholders with data from plugin.cpp
    if (!loadPlugin(plugin)) 
    {
        std::cerr << "Failed initial plugin load\n";
        return 1;
    }

    // setup RtAudio output stream
    RtAudio dac;
    if (dac.getDeviceCount() < 1) 
    {
        std::cerr << "No audio devices found!\n";
        return 1;
    }

    // configure output stream parameters, default device & stereo output
    RtAudio::StreamParameters streamParams;
    streamParams.deviceId = dac.getDefaultOutputDevice();
    streamParams.nChannels = 2;

    // start UI (and potentially wavWriter) on another thread
    std::thread ui(uiThread);
    ui.detach();

    // start audio stream
    try
    {
        dac.openStream(&streamParams,       // output stream parameters
                       nullptr,             // no input stream
                       RTAUDIO_FLOAT32,     // sample format
                       SAMPLERATE,
                       &rtBufferFrames,     // sample frames per callback
                       callback,            // audio callback function name
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

    int firstTime = 0;
    std::filesystem::file_time_type lastWriteTime;

    // periodically check plugin.h file for changes
    while (true) 
    {
        auto currentTime = std::filesystem::last_write_time(PLUGINSOURCE);

        // don't trigger rebuild on firstLoop
        if (!firstTime) 
        {
            lastWriteTime = currentTime;
            firstTime = 1;
        }

        // when file edited & not currently reloading, reload on new thread
        if ((currentTime != lastWriteTime) && !globals.reloading.load())
        {
            // prevent double reloads
            globals.reloading.store(1);
            lastWriteTime = currentTime;

            logBuff.setNewLine("RELOADING PLUGIN");
            std::thread reload(reloadPluginThread);
            reload.detach();
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }

    // clean up (usually unreachable)
    dac.closeStream();
}
