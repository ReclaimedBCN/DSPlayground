// Copyright 2026 Reclaimed BCN. All rights reserved.
// Use of this source code is governed by the license found in the LICENSE file.

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <dlfcn.h>
#include <unistd.h>

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
std::atomic_flag pluginInUse = ATOMIC_FLAG_INIT;
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
// Load new plugin module without modifying active one
// ----------------------------------------------------------------------------------------------
bool loadPlugin(PluginModule& module)
{
    static unsigned long long generation = 0;
    const std::string pluginPath = "./build/plugins/" + sharedLibraryName("plugin");
    std::string loadPath = pluginPath + ".reload-" + std::to_string(++generation) + "-XXXXXX";

    const int descriptor = mkstemp(loadPath.data());
    if (descriptor == -1)
    {
        std::cerr << "Failed to reserve a plugin copy: " << std::strerror(errno) << '\n';
        return false;
    }
    close(descriptor);

    // try to open the shared library file
    void* handle = nullptr;
    std::error_code copyError;
    std::filesystem::copy_file(pluginPath, loadPath, std::filesystem::copy_options::overwrite_existing, copyError);
    if (copyError) 
    {
        std::cerr << "Failed to copy the plugin: " << copyError.message() << '\n';
    }
    else
    {
        handle = dlopen(loadPath.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (!handle) std::cerr << "Failed to load the plugin: " << dlerror() << '\n';
    }

    // loader retains its mapping after temporary copy is unlinked
    if (unlink(loadPath.c_str()) != 0)
    {
        std::cerr << "Failed to remove " << loadPath << ": " << std::strerror(errno) << '\n';
    }
    if (!handle) return false;

    // get handles via exported function names from plugin.cpp
    const auto createFn = reinterpret_cast<decltype(module.create)>(dlsym(handle, "createPlugin"));
    const auto destroyFn = reinterpret_cast<decltype(module.destroy)>(dlsym(handle, "destroyPlugin"));
    const auto processFn = reinterpret_cast<decltype(module.process)>(dlsym(handle, "processPlugin"));

    // check all functions were found
    if (!createFn || !destroyFn || !processFn)
    {
        std::cerr << "The plugin must export createPlugin, destroyPlugin, and processPlugin\n";
        dlclose(handle);
        return false;
    }

    // create new PluginState
    void* state = nullptr;
    try
    {
        state = createFn(&uiParams);
    }
    catch (...)
    {
        std::cerr << "Error while creating plugin's DSP state\n";
        dlclose(handle);
        return false;
    }
    if (!state)
    {
        std::cerr << "Plugin returned an empty DSP state\n";
        dlclose(handle);
        return false;
    }
    // store new pointers
    module = {handle, state, createFn, destroyFn, processFn};
    return true;
}

void destroyPlugin(PluginModule& module)
{
    if (module.state) module.destroy(module.state);
    if (module.handle) dlclose(module.handle);
    module = {};
}

void swapPlugin(PluginModule& replacement)
{
    // incase RtAudio backend still has a callback in flight after stopStream()
    while (pluginInUse.test_and_set(std::memory_order_acquire)) std::this_thread::yield();
    std::swap(plugin, replacement);
    pluginInUse.clear(std::memory_order_release);
}

// -------------------------------------------------------------------------
// RtAudio callback, called by RtAudio when it needs more samples
// -------------------------------------------------------------------------
int callback(void* outBuffer, void*, unsigned int numFrames, double, RtAudioStreamStatus, void* userData)
{
    if (userData)
    {
        const auto& module = *static_cast<PluginModule*>(userData);
        float* outFloats = static_cast<float*>(outBuffer);
        const auto numSamples = 2 * static_cast<std::size_t>(numFrames);

        if (pluginInUse.test_and_set(std::memory_order_acquire)) // plugin busy, use silence
        {
            std::fill_n(outFloats, numSamples, 0.0f);
        }
        else // plugin free, generate samples
        {
            if (module.process && module.state)
            {
                module.process(module.state, outFloats, static_cast<int>(numFrames));
            }
            else
            {
                std::fill_n(outFloats, numSamples, 0.0f);
            }
            pluginInUse.clear(std::memory_order_release);
        }

        // write output to the circular buffer for UI and recorder
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
void reloadPlugin(RtAudio& dac)
{
    // rebuild dynamic library
    if (std::system("cmake --build build --target plugin") != 0) 
    {
        logBuff.setNewLine("Plugin build failed. Using the previous plugin");
        return;
    }
    // load into new module
    PluginModule replacement{};
    if (!loadPlugin(replacement))
    {
        logBuff.setNewLine("Plugin load failed. Using the previous plugin");
        return;
    }
    // stop stream to unload
    if (dac.isStreamRunning() && dac.stopStream() != RTAUDIO_NO_ERROR)
    {
        logBuff.setNewLine("Could not stop audio for reload: " + dac.getErrorText());
        destroyPlugin(replacement);
        if (dac.isStreamOpen() && !dac.isStreamRunning() && dac.startStream() != RTAUDIO_NO_ERROR)
        {
            logBuff.setNewLine("Could not resume audio with the previous plugin: " + dac.getErrorText());
        }
        return;
    }
    // swap / revert
    swapPlugin(replacement);
    if (dac.startStream() != RTAUDIO_NO_ERROR)
    {
        logBuff.setNewLine("Could not start audio with the new plugin: " + dac.getErrorText());
        swapPlugin(replacement);
        destroyPlugin(replacement);
        if (dac.startStream() != RTAUDIO_NO_ERROR)
        {
            logBuff.setNewLine("Could not resume audio with the previous plugin: " + dac.getErrorText());
        }
        return;
    }
    // destroy old
    destroyPlugin(replacement);
    logBuff.setNewLine("Plugin reloaded successfully");
}

// -----------------------------------------------------------------------------
// Entry point
// -----------------------------------------------------------------------------
int main()
{
    // timestamp plugin
    std::error_code pluginError;
    auto lastWriteTime = std::filesystem::last_write_time(PLUGINSOURCE, pluginError);

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
        destroyPlugin(plugin);
        return 1;
    }

    // configure output stream parameters, default device & stereo output
    RtAudio::StreamParameters streamParams;
    streamParams.deviceId = dac.getDefaultOutputDevice();
    streamParams.nChannels = 2;

    // start audio stream
    const auto openResult = dac.openStream (&streamParams,       // output stream parameters
                                            nullptr,             // no input stream
                                            RTAUDIO_FLOAT32,     // sample format
                                            SAMPLERATE,
                                            &rtBufferFrames,     // sample frames per callback
                                            callback,            // audio callback function name
                                            &plugin);            // userData to pass to callback
    if (dac.startStream() != RTAUDIO_NO_ERROR || openResult != RTAUDIO_NO_ERROR)
    {
        std::cerr << "Failed to start audio: " << dac.getErrorText() << '\n';
        if (dac.isStreamOpen()) dac.closeStream();
        destroyPlugin(plugin);
        return 1;
    }
    logBuff.setNewLine("Audio stream running");
    logBuff.setNewLine("Edit plugin.h to hear changes live");

    // start UI (and potentially wavWriter) on another thread
    std::thread ui(uiThread);
    ui.detach();

    // periodically check plugin.h file for changes
    while (true)
    {
        const auto currentTime = std::filesystem::last_write_time(PLUGINSOURCE, pluginError);
        if (!pluginError && currentTime != lastWriteTime)
        {
            lastWriteTime = currentTime;
            logBuff.setNewLine("RELOADING PLUGIN");
            reloadPlugin(dac);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}
