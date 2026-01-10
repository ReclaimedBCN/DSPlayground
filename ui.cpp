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

    auto logTest = [](LogBuffer& logBuff)
    {
        Dimensions termDim = Terminal::Size();
        const int termWidth = termDim.dimx;
        const int termHeight = termDim.dimy;
        logBuff.setNewLine("termX" + std::to_string(termWidth) + " termY" + std::to_string(termHeight));
    };

    // -- Toggles ---------------------------------------------------------------
    bool checkbox_2_selected = false;
    bool checkbox_3_selected = false;
    bool checkbox_4_selected = false;

    auto toggles = Container::Vertical(
    {
        Checkbox("bypass", &uiParams.bypass),
        Checkbox("checkbox2", &checkbox_2_selected),
        Checkbox("checkbox3", &checkbox_3_selected),
        Checkbox("checkbox4", &checkbox_4_selected),
    });
    toggles = Wrap("Toggles", toggles);

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
    auto sliders = Container::Vertical(
    {
        // args = name, current value, min, max, increment
        Slider("Freq:", &uiParams.freq, 0.f, 127.f, 1.f) | color(Color::Blue),
        Slider("Gain:", &uiParams.gain, 0.f, 127.f, 1.f) | color(Color::Magenta),
        Slider("Phase:", &uiParams.phase, 0.f, 127.f, 1.f) | color(Color::Yellow),
    });
    sliders = Wrap("Sliders", sliders);

    auto paramNumbers = [](int v1, int v2, int v3)
    {
        return text(
            "freq: " + std::to_string(v1) 
            + ", vol: " + std::to_string(v2) 
            + ", phase: " + std::to_string(v3)
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
                paramNumbers(uiParams.freq, uiParams.gain, uiParams.phase),

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
}
