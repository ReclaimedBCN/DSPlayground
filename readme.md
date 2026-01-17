```
 ______   _______ _______ __                                             __
|   _  \ |   _   |   _   |  .---.-.--.--.-----.----.-----.--.--.-----.--|  |
|.  |   \|   1___|.  1   |  |  _  |  |  |  _  |   _|  _  |  |  |     |  _  |
|.  |    |____   |.  ____|__|___._|___  |___  |__| |_____|_____|__|__|_____|
|:  1    |:  1   |:  |            |_____|_____|
|::.. . /|::.. . |::.|
`------' `-------`---'
```
![DSPlayground Demo](./assets/DSPlaygroundDemo.gif)

## Why Bother?

- Lightweight terminal environment for live coding DSP in C++ without any guard rails or limitations
- Fast iteration with runtime hot loading of new code with no audio dropouts. Auto-loads DSP code on file save
- Find sweet spots in real time with the Terminal UI parameter controls
- Realtime visual feedback with oscilloscope visualisers for rapid debugging
- Export .wav recordings for hi-def post-analysis
- Realtime-safe multithreading preconfigured so you can safely pass data between the worker thread and the audio thread without glitches or data races
- Use your favorite text editor
- Prototype DSP code in a simple sandboxed environment similar to your deployment environment but without the long compile time waits
- No more porting protoypes from another language!
- Codebase carefully commented so you can take your protoypes as far as you like or keep them simple, up to you

## Platform Support

- Built and tested on MacOS with Apple Silicon
- Considerations for Linux anomalies included (untested though!)
- Windows not currently supported (if you’d like to help with this we’d love to hear from you)

## Dependencies / Recommendations

- You’ll need an existing C++ build environment, if you’re programming in C++ you probably already have these
	- GCC / Clang
	- CMake
	- Make
- Submodules
    - RtAudio for handling interface with soundcard for the audio callback
    - FTXUI for the terminal UI
        - If you’re still using the prebundled Terminal app it’s time for an upgrade!
        - You'll want a modern one for proper display rendering (among many other things)
        - Popular modern terminal emulators = Ghostty, Alacritty
- Any text editor
	- Neovim + Tmux is a pretty killer workflow though!

---

## Installation

```bash
git clone --recursive "INSERT GITHUB LINK"
```

> You can either choose to install RtAudio and FTXUI on your system or build the libraries locally in their submodule folders. Installing system-wide will mean faster build times but the choice is up to you.

### Building locally (the default)

```bash
# build from DSPlayground repo root directory
cmake -S . -B build
cmake --build build --parallel
```

### Installing system-wide

- In the `external/` directory, enter the RtAudio and FTXUI submodule folders
- Read their README files / documentation and install them system-wide based on their instructions. Both of them support CMake so you can run cmake commands from their repo directories
- Now return to the DSPlayground repo directory and edit the CMakeLists.txt to enable the options below

```cmake
option(USE_SYSTEM_RTAUDIO "Use system-wide install of rtaudio" ON)
option(USE_SYSTEM_FTXUI "Use system-wide install of FTXUI" ON)
```

- Build DSPlayground

```bash
# build from DSPlayground repo root directory
cmake -S . -B build
cmake --build build --parallel
```

## Usage

Before running DSPlayground you'll want to turn your system's volume all the way down. RtAudio's output can be quite loud.

```bash
# start DSPlayground from the repo's root directory
./run.sh
```

- Now you can navigate the UI with your keyboard. Vim keybindings, arrow keys and enter and supported. Or if you prefer, you can use your mouse too.
- Use the controls to change the parameters of the audio engine in real-time
- Open up plugin.h in a text editor
- Make changes to the algorithm, when you save the file the DSP code will be hot reloaded.

FYI, the default values of the UI sliders determine the initial state of the parameters that they control when you first run DSPlayground.
These can be changed in the globals.h file in the UiParams struct. You will need to recompile the binary (not the whole project) for this to take affect.

```bash
# recompile the binary when making changes to source files other than plugin.h
# from the repo's root directory
cmake --build build --parallel

# rebuild the project when making changes to the project structure
    # e.g. adding files / changing CMakeLists.txt
# from the repo's root directory
cmake -S . -B build
cmake --build build --parallel
```

Have fun and experiment away!

p.s. To leave the DSPlayground, use the close buttons to close the UI and `ctrl + c` to end the audio stream. Or just `ctrl + c` twice. Long live the terminal!

---

## Future  Plans

- [ ] Linux support
- [ ] MIDI support
- [ ] Windows support

If you’d like to help with any of the above we are open to pull requests and collaboration :)

## Thanks

Big thanks to @thestk and @ArthurSonzogni for RtAudio and FTXUI respectively. Both amazing projects that deserve some recognition. Also thanks to @Thrifleganger for the .wav encoding tutorial!

## License

The DSPlayground license is similar to the MIT License. Please see [LICENSE](LICENSE).

```
▄▄▄  ▄▄▄ . ▄▄· ▄▄▌   ▄▄▄· ▪  • ▌ ▄ ·. ▄▄▄ .·▄▄▄▄      ▄▄▄▄·  ▄▄·  ▐ ▄
▀▄ █·▀▄.▀·▐█ ▌▪██•  ▐█ ▀█ ██ ·██ ▐███▪▀▄.▀·██▪ ██     ▐█ ▀█▪▐█ ▌▪•█▌▐█
▐▀▀▄ ▐▀▀▪▄██ ▄▄██▪  ▄█▀▀█ ▐█·▐█ ▌▐▌▐█·▐▀▀▪▄▐█· ▐█▌    ▐█▀▀█▄██ ▄▄▐█▐▐▌
▐█•█▌▐█▄▄▌▐███▌▐█▌▐▌▐█ ▪▐▌▐█▌██ ██▌▐█▌▐█▄▄▌██. ██     ██▄▪▐█▐███▌██▐█▌
.▀  ▀ ▀▀▀ ·▀▀▀ .▀▀▀  ▀  ▀ ▀▀▀▀▀  █▪▀▀▀ ▀▀▀ ▀▀▀▀▀•     ·▀▀▀▀ ·▀▀▀ ▀▀ █▪

```
Copyright 2026 Reclaimed BCN. All rights reserved.

### Disclaimer

The software is provided "as is", without warranty of any kind, express or
implied, including but not limited to the warranties of merchantability,
fitness for a particular purpose and noninfringement. In no event shall
the authors or copyright holders be liable for any claim, damages or other
liability, whether in an action of contract, tort or otherwise, arising
from, out of or in connection with the software or the use or other
dealings in the software.

