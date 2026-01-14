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

ftxui::Element Ascii() { return ftxui::paragraph(R"(
▄▄▄  ▄▄▄ . ▄▄· ▄▄▌   ▄▄▄· ▪  • ▌ ▄ ·. ▄▄▄ .·▄▄▄▄      ▄▄▄▄·  ▄▄·  ▐ ▄
▀▄ █·▀▄.▀·▐█ ▌▪██•  ▐█ ▀█ ██ ·██ ▐███▪▀▄.▀·██▪ ██     ▐█ ▀█▪▐█ ▌▪•█▌▐█
▐▀▀▄ ▐▀▀▪▄██ ▄▄██▪  ▄█▀▀█ ▐█·▐█ ▌▐▌▐█·▐▀▀▪▄▐█· ▐█▌    ▐█▀▀█▄██ ▄▄▐█▐▐▌
▐█•█▌▐█▄▄▌▐███▌▐█▌▐▌▐█ ▪▐▌▐█▌██ ██▌▐█▌▐█▄▄▌██. ██     ██▄▪▐█▐███▌██▐█▌
.▀  ▀ ▀▀▀ ·▀▀▀ .▀▀▀  ▀  ▀ ▀▀▀▀▀  █▪▀▀▀ ▀▀▀ ▀▀▀▀▀•     ·▀▀▀▀ ·▀▀▀ ▀▀ █▪
 ______   _______ _______ __                                             __
|   _  \ |   _   |   _   |  .---.-.--.--.-----.----.-----.--.--.-----.--|  |
|.  |   \|   1___|.  1   |  |  _  |  |  |  _  |   _|  _  |  |  |     |  _  |
|.  |    |____   |.  ____|__|___._|___  |___  |__| |_____|_____|__|__|_____|
|:  1    |:  1   |:  |            |_____|_____|
|::.. . /|::.. . |::.|
`------' `-------`---')")
| color(ftxui::Color::HotPink2);
}

ftxui::Element ascii() { return ftxui::paragraph(R"(
 ______   _______ _______ __                                             __
|   _  \ |   _   |   _   |  .---.-.--.--.-----.----.-----.--.--.-----.--|  |
|.  |   \|   1___|.  1   |  |  _  |  |  |  _  |   _|  _  |  |  |     |  _  |
|.  |    |____   |.  ____|__|___._|___  |___  |__| |_____|_____|__|__|_____|
|:  1    |:  1   |:  |            |_____|_____|
|::.. . /|::.. . |::.|
`------' `-------`---')")
| color(ftxui::Color::HotPink2);
}

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
        logBuff.setNewLine("You can print any value to the console! Here's the terminal dimensions");
        logBuff.setNewLine("X = " + std::to_string(termWidth) + " Y = " + std::to_string(termHeight));
    };

    // -- Toggles ---------------------------------------------------------------
    bool toggle1 = uiParams.bypass;
    bool toggle2 = false;
    bool toggle3 = false;
    bool toggle4 = false;

    auto toggles = Container::Horizontal(
    {
        Checkbox("bypass ", &toggle1),
        Checkbox("toggle2 ", &toggle2),
        Checkbox("toggle3 ", &toggle3),
        Checkbox("toggle4 ", &toggle4),
    });

    // Detect changes
    auto togglesCallback = ftxui::CatchEvent(toggles,
        [&](ftxui::Event event) 
        {
            bool handled = toggles->OnEvent(event);
            // If the event changed something, update shared atomic variables
            if (handled) { updateAtomicsCheckbox(toggle1, toggle2, toggle3, toggle4); }
            return handled;
        }
    );

    togglesCallback = Wrap("Toggles", togglesCallback);

    // -- Buttons -----------------------------------------------------------------
    int tab_index = 0;
    auto buttons = Container::Horizontal(
    {
        // ButtonOption::Animated(Color::Orange4)
        // ButtonOption::Animated(Color::DeepSkyBlue4)
        // ButtonOption::Animated(Color::DarkRed) 
        Button("Record WAV", [&] 
        { 
            std::thread wavWrite(wavWriteThread);
            wavWrite.detach(); // run independently
        }, ButtonOption::Ascii()) | xflex_grow,
        Button("Press Me", [&] { logTest(logBuff); }, ButtonOption::Ascii()) | xflex_grow,
        Button("Close", [&] { screen.Exit(); }, ButtonOption::Ascii()) | xflex_grow,
    });

    buttons = Wrap("Buttons", buttons);

    // -- Sliders -----------------------------------------------------------------
    float sliderVal1 = uiParams.freq;
    float sliderVal2 = uiParams.gain;

    SliderOption<float> slider1;
    slider1.value = &sliderVal1;
    slider1.min = 20.f;
    slider1.max = 2000.f;
    slider1.increment = 8.f;
    slider1.direction = Direction::Right;
    slider1.color_active = Color::White;
    slider1.color_inactive = Color::Magenta;

    SliderOption<float> slider2;
    slider2.value = &sliderVal2;
    slider2.min = 0.f;
    slider2.max = 0.99f;
    slider2.increment = 0.05f;
    slider2.direction = Direction::Right;
    slider2.color_active = Color::White;
    slider2.color_inactive = Color::LightSkyBlue3;

    auto sliders = Container::Vertical(
    {
        Slider(slider1),
        Slider(slider2),
    });

    // Detect changes
    auto slidersCallback = ftxui::CatchEvent(sliders,
        [&](ftxui::Event event) 
        {
            bool handled = sliders->OnEvent(event);
            // If the event changed something, update shared atomic variables
            if (handled) { updateAtomicsSlider(sliderVal1, sliderVal2); }
            return handled;
        }
    );

    slidersCallback = Wrap("Sliders", slidersCallback);


    auto sliderReadout = [&](float slider1, float slider2)
    {
        return text(
            "freq: " + std::to_string(slider1) 
            + ", gain: " + std::to_string(slider2) 
        ) | dim;
    };

    auto spacer = Spacer();

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
                // separator(),
                togglesCallback->Render(),
                spacer->Render(),
                slidersCallback->Render(),
                spacer->Render(),
                buttons->Render(),

                separator(),
                sliderReadout(sliderVal1, sliderVal2),

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
                logBuff.getMiniLog() | yflex,
                ascii() | align_right,
                // | flex,
            }); 
            // | xflex | size(WIDTH, GREATER_THAN, 40) | borderEmpty;

    });

    auto logTab = Renderer([&] 
    { 
        return vbox
        ({
            logBuff.getFullLog() | yflex,
            separatorEmpty(),
            Ascii() | align_right,
        });
    });

    // TABS

    std::vector<std::string> tab_entries = {
        "Params", "Full Log",
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
        Button("Close", [&] { screen.Exit(); }, ButtonOption::Animated()),
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
