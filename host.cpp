// -----------------------------------------------------------------------------
// RtAudio Host with DSP Plugin Hot-Reload:
    // flow: inital load, audio setup, dynamic reload loop, cleanup
// -----------------------------------------------------------------------------
#include <iostream>
#include <thread>
#include <filesystem>
#include <chrono>
#include <dlfcn.h>          // for dlopen, dlsym, dlclose

#include "RtAudio.h"        // RtAudio for cross-platform audio I/O

#include "globals.h"
#include "plugin.h"         // Shared class for DSP parameters
#include "wavParser.h"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/component_options.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/component/loop.hpp"
#include "ftxui/dom/elements.hpp"
#include "ftxui/dom/canvas.hpp"
#include "ftxui/screen/color.hpp"

#include "ftxui/component/mouse.hpp"
#include <cmath>     // for sin
#include <functional>  // for ref, reference_wrapper, function
#include <memory>      // for allocator, shared_ptr, __shared_ptr_access
#include <string>  // for string, basic_string, char_traits, operator+, to_string
#include <utility>  // for move
#include <vector>   // for vector

static_assert (std::atomic<float>::is_always_lock_free); // check float type is lock free

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
Globals globals;
unsigned int rtBufferFrames = BUFFERFRAMES; // assign constant to mutable as RtAudio will change value if unsupported by system

LogBuffer logBuff; // circular buffer for logging standard output

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

    logBuff.setNewLine("Plugin reloaded successfully");
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
void wavWriteThread() { writeWav(globals, logBuff); }

// -------------------------------------------------------------------------
// Asynchronous function for realtime parameter updates
// -------------------------------------------------------------------------
void uiThread()
{
    // if (globals.reloading.load() == 0) std::cout << "REPL ready. Try commands like: <parameter> <value>" << std::endl;

    using namespace ftxui;

    auto screen = ScreenInteractive::Fullscreen();

    auto Wrap = [](std::string name, Component component) 
    {
        return Renderer(component, [name, component] 
        {
            return hbox(
            {
                text(name) | size(WIDTH, EQUAL, 8),
                separator(),
                component->Render() | xflex,
            }) | xflex;
        });
    };

    auto Spacer = []() 
    {
        return Renderer([] 
        {
            return hbox(
            {
                text("") | size(WIDTH, EQUAL, 8),
                separator(),
            }) | xflex;
        });
    };

    auto logTest = []()
    {
        Dimensions termDim = Terminal::Size();
        const int termWidth = termDim.dimx;
        const int termHeight = termDim.dimy;
        logBuff.setNewLine("termX" + std::to_string(termWidth) + " termY" + std::to_string(termHeight));
    };

    // -- Toggles ---------------------------------------------------------------
    bool checkbox_1_selected = false;
    bool checkbox_2_selected = false;
    bool checkbox_3_selected = false;
    bool checkbox_4_selected = false;

    auto toggles = Container::Vertical(
    {
        Checkbox("checkbox1", &checkbox_1_selected),
        Checkbox("checkbox2", &checkbox_2_selected),
        Checkbox("checkbox3", &checkbox_3_selected),
        Checkbox("checkbox4", &checkbox_4_selected),
    });
    toggles = Wrap("Toggles", toggles);

    // -- Buttons -----------------------------------------------------------------
    int tab_index = 0;
    auto buttons = Container::Horizontal(
    {
        Button("Reset", [&] { logTest(); }, ButtonOption::Animated(Color::Orange4)) | xflex_grow,
        Button("Log", [&] { tab_index = 1; }, ButtonOption::Animated(Color::DeepSkyBlue4)) | xflex_grow,
        Button("Record WAV", [&] { screen.Exit(); }, ButtonOption::Animated(Color::DarkRed)) | xflex_grow,
    });
    buttons = Wrap("Buttons", buttons);

    // -- Sliders -----------------------------------------------------------------
    int slider_value_1 = 12;
    int slider_value_2 = 56;
    int slider_value_3 = 78;
    auto sliders = Container::Vertical(
    {
        // args = name, current value, min, max, increment
        Slider("Freq:", &slider_value_1, 0, 127, 1) | color(Color::Blue),
        Slider("Vol:", &slider_value_2, 0, 127, 1) | color(Color::Magenta),
        Slider("Phase:", &slider_value_3, 0, 127, 1) | color(Color::Yellow),
    });
    sliders = Wrap("Sliders", sliders);

    auto paramNumbers = [](int v1, int v2, int v3)
    {
        return text(
            "freq: " + std::to_string(v1) 
            + ", Vol: " + std::to_string(v2) 
            + ", Phase: " + std::to_string(v3)
        ) | dim;
    };

    auto spacer = Spacer();

    // mouse co-ords
    int mouse_x = 0;
    int mouse_y = 0;

    auto braillePlot = Renderer([&] 
    {
        Dimensions termDim = Terminal::Size();
        const int plotWidth = (termDim.dimx * 1.5) - 8; // 8 for (+1, 0, -1) axis guide
        const int plotHeight = (termDim.dimy * 1.5);

        auto plot = Canvas(plotWidth, plotHeight);
        plot.DrawText(0, 0, "Waveform", Color::Grey50);

        std::vector<int> ys(plotWidth);

        for (int x=0; x<plotWidth; x++) 
        {
            float dx = float(x - mouse_x);
            float dy = static_cast<float>(plotHeight) * 0.5;
            ys[x] = int(dy + 20 * cos(dx * 0.14) + 10 * sin(dx * 0.42));
        }

        for (int x=1; x<plotWidth-1; x++) plot.DrawPointLine(x, ys[x], x + 1, ys[x + 1]);

        return canvas(std::move(plot));
    });

    auto filledPlot = Renderer([&] 
    {
        Dimensions termDim = Terminal::Size();
        const int plotWidth = (termDim.dimx * 1.5) - 8; // 8 for (+1, 0, -1) axis guide
        const int plotHeight = (termDim.dimy * 1.5);

        auto plot = Canvas(plotWidth, plotHeight);
        plot.DrawText(0, 0, "Absolute Waveform", Color::Grey50);

        std::vector<int> ys(plotWidth);

        for (int x=0; x<plotWidth; x++) 
        {
            ys[x] = int(30 + 10 * cos(x * 0.2 - mouse_x * 0.05)
                        + 5 * sin(x * 0.4) +
                        5 * sin(x * 0.3 - mouse_y * 0.05));
        }

        int halfHeight = plotHeight * 0.5;
        for (int x=0; x<plotWidth; x++) 
        {
            plot.DrawPointLine(x, halfHeight + ys[x], x, halfHeight - ys[x], Color::Red);
        }

        return canvas(std::move(plot));
    });

    auto plots = Container::Horizontal(
        {
            braillePlot,
            filledPlot,
        });

    // Capture the last mouse position
    auto mousePos = CatchEvent(plots, [&](Event e) {
    if (e.is_mouse()) 
    {
        mouse_x = (e.mouse().x - 1) * 2;
        mouse_y = (e.mouse().y - 1) * 4;
    }
    return false;
    });

    // RENDERER
    auto plotRenderer = Renderer(mousePos, [&] 
    {
        return hbox(
        {
            braillePlot->Render(),
            filledPlot->Render(),
        }) | borderEmpty;
    });

    // -- Layout ----------------------
    auto layout = Container::Vertical({
        toggles,
        sliders,
        buttons,
        plotRenderer,
    });

    auto paramsTab = Renderer(layout, [&] {
    return vbox({
                separator(),
                toggles->Render(),
                spacer->Render(),
                sliders->Render(),
                spacer->Render(),
                buttons->Render(),

                separator(),
                paramNumbers(slider_value_1, slider_value_2, slider_value_3),

                separator(),
                hbox({
                    // filler(),
                    vbox({
                        separatorEmpty(),
                        text("1 ") | center | size(WIDTH, EQUAL, 8),
                        filler(),
                        text("0 ") | center | size(WIDTH, EQUAL, 8),
                        filler(),
                        text("-1 ") | center | size(WIDTH, EQUAL, 8),
                        separatorEmpty(),
                    }),
                    // | xflex,
                    // filler(),
                    separator(),
                    plotRenderer->Render(),
                }),
                separator(),
                logBuff.getMiniLog(),
                // | flex,
            }); 
            // | xflex | size(WIDTH, GREATER_THAN, 40) | borderEmpty;

    });

    auto logTab = Renderer([] { return logBuff.getFullLog(); });

    // TABS

    std::vector<std::string> tab_entries = {
        "Params", "Log",
    };

    auto option = MenuOption::HorizontalAnimated();
    option.underline.SetAnimation(std::chrono::milliseconds(150),
                                animation::easing::BackOut);
    auto tab_selection =
        Menu(&tab_entries, &tab_index, option);
    auto tab_content = Container::Tab(
        {
            paramsTab,
            logTab,
            // Renderer(mousePos, [&] { return renderer_plot_1->Render(); })
            // optionsTab
        },
        &tab_index);

    // SCREEN & WINDOW RENDER

    auto exit_button = Container::Horizontal({
        Button("Exit", [&] { screen.Exit(); }, ButtonOption::Animated()),
    });

    auto tab_container = Container::Vertical({
        Container::Horizontal({
            tab_selection,
            exit_button,
        }),
        tab_content,
    });

    auto main_renderer = Renderer(tab_container, [&] {
        return vbox({
            text("dspPlayground") | bold | hcenter,
            hbox({
                tab_selection->Render() | flex,
                exit_button->Render(),
            }),
            tab_content->Render() | flex,
        });
    });

    screen.Loop(main_renderer);

    // start REPL loop
    while (true) 
    {
        /*
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
        else if (param == "rec")
        {
            std::thread wavWrite(wavWriteThread);
            wavWrite.detach(); // run independently
        }
        */
    }
    // std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    // uiThread();
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

    // start UI (and potentially wavWriter) in background
    std::thread ui(uiThread);
    ui.detach(); // run independently

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
    logBuff.setNewLine("Edit plugin.cpp to hear changes live");

    // -------------------------------------------------------------------------
    // Main loop: Start UI thread, check for plugin file changes
    // -------------------------------------------------------------------------
    // setup
    int firstTime = 0;
    std::filesystem::file_time_type lastWriteTime;

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

            logBuff.setNewLine("RELOADING PLUGIN");
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
