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
#include <memory>      // for allocator, shared_ptr, __shared_ptr_access
#include <string>  // for string, basic_string, char_traits, operator+, to_string
#include <utility>  // for move
#include <vector>   // for vector

#include <thread>

#include "globals.h"
#include "wavParser.h"

void drawUi(LogBuffer& logBuff, Globals& globals, UiParams& uiParams)
{
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

    // update shared atomic variables when ui changes
    auto updateAtomicsCheckbox = [&](bool checkbox1, bool checkbox2, bool checkbox3, bool checkbox4)
    {
        uiParams.bypass = checkbox1;
    };
    auto updateAtomicsSlider = [&](float slider1, float slider2)
    {
        uiParams.freq = slider1;
        uiParams.gain = slider2;
    };

    auto logTest = [](LogBuffer& logBuff)
    {
        Dimensions termDim = Terminal::Size();
        const int termWidth = termDim.dimx;
        const int termHeight = termDim.dimy;
        logBuff.setNewLine("termX" + std::to_string(termWidth) + " termY" + std::to_string(termHeight));
    };

    // -- Toggles ---------------------------------------------------------------
    bool checkbox1 = uiParams.bypass;
    bool checkbox2 = false;
    bool checkbox3 = false;
    bool checkbox4 = false;
    auto toggles = Container::Vertical(
    {
        Checkbox("bypass", &checkbox1),
        Checkbox("checkbox2", &checkbox2),
        Checkbox("checkbox3", &checkbox3),
        Checkbox("checkbox4", &checkbox4),
    });

    // Detect changes
    auto togglesCallback = ftxui::CatchEvent(toggles,
        [&](ftxui::Event event) 
        {
            bool handled = toggles->OnEvent(event);
            // If the event changed something, update shared atomic variables
            if (handled) { updateAtomicsCheckbox(checkbox1, checkbox2, checkbox3, checkbox4); }
            return handled;
        }
    );

    togglesCallback = Wrap("Toggles", togglesCallback);

    // -- Buttons -----------------------------------------------------------------
    int tab_index = 0;
    auto buttons = Container::Horizontal(
    {
        Button("Reset", [&] { logTest(logBuff); }, ButtonOption::Animated(Color::Orange4)) | xflex_grow,
        Button("Log", [&] { tab_index = 1; }, ButtonOption::Animated(Color::DeepSkyBlue4)) | xflex_grow,
        Button("Record WAV", [&] 
        { 
            std::thread wavWrite(wavWriteThread);
            wavWrite.detach(); // run independently
        }, ButtonOption::Animated(Color::DarkRed)) | xflex_grow,
    });

    buttons = Wrap("Buttons", buttons);

    // -- Sliders -----------------------------------------------------------------
    float slider1 = uiParams.freq;
    float slider2 = uiParams.gain;
    auto sliders = Container::Vertical(
    {
        // args = name, current value, min, max, increment
        Slider("Freq:", &slider1, 20.f, 2000.f, 8.f) | color(Color::Blue),
        Slider("Gain:", &slider2, 0.f, 0.99f, 0.05f) | color(Color::Magenta),
        // Slider("Phase:", &uiParams.phase, 0.f, 127.f, 1.f) | color(Color::Yellow),
    });

    // Detect changes
    auto slidersCallback = ftxui::CatchEvent(sliders,
        [&](ftxui::Event event) 
        {
            bool handled = sliders->OnEvent(event);
            // If the event changed something, update shared atomic variables
            if (handled) { updateAtomicsSlider(slider1, slider2); }
            return handled;
        }
    );

    slidersCallback = Wrap("Sliders", slidersCallback);

    auto sliderReadout = [](float slider1, float slider2)
    {
        return text(
            "freq: " + std::to_string(slider1) 
            + ", gain: " + std::to_string(slider2) 
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

        int a = globals.writeHead.load() - static_cast<int>(plotWidth);
        int m = globals.circularOutput.size();
        int readHeadStart = ((a % m) + m) % m;
        const int plotHalfHeight = plotHeight * 0.5;

        for (int x=0; x<plotWidth; x++) 
        {
            int readHead = (readHeadStart + x) % globals.circularOutput.size();
            ys[x] = globals.circularOutput[readHead] * plotHalfHeight + plotHalfHeight;
        }

        screen.RequestAnimationFrame();

        for (int x=1; x<plotWidth-1; x++) 
        {
            plot.DrawPointLine(x, ys[x], x + 1, ys[x + 1], Color::PaleGreen1); // PaleGreen1 Aquamarine3 LightGreen Cyan2 PaleGreen3 GreenLight
        }

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

        int a = globals.writeHead.load() - static_cast<int>(plotWidth);
        int m = globals.circularOutput.size();
        int readHeadStart = ((a % m) + m) % m;
        const int plotHalfHeight = plotHeight * 0.5;

        for (int x=0; x<plotWidth; x++) 
        {
            int readHead = (readHeadStart + x) % globals.circularOutput.size();
            ys[x] = fabsf(globals.circularOutput[readHead]) * plotHalfHeight;
        }

        screen.RequestAnimationFrame();

        for (int x=0; x<plotWidth; x++) 
        {
            plot.DrawPointLine(x, plotHalfHeight + ys[x], x, plotHalfHeight - ys[x], Color::HotPink2); // HotPink2 HotPink Red
        }

        return canvas(std::move(plot));
    });

    auto plots = Container::Horizontal(
        {
            braillePlot,
            filledPlot,
        });

    // RENDERER
    auto plotRenderer = Renderer([&] 
    {
        return hbox(
        {
            braillePlot->Render(),
            filledPlot->Render(),
        }) | borderEmpty;
    });

    // -- Layout ----------------------
    auto layout = Container::Vertical({
        togglesCallback,
        slidersCallback,
        buttons,
        plotRenderer,
    });

    auto paramsTab = Renderer(layout, [&] {
    return vbox({
                separator(),
                togglesCallback->Render(),
                spacer->Render(),
                slidersCallback->Render(),
                spacer->Render(),
                buttons->Render(),

                separator(),
                sliderReadout(slider1, slider2),

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

    auto logTab = Renderer([&] { return logBuff.getFullLog(); });

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
            // Renderer([&] { return renderer_plot_1->Render(); })
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
}
