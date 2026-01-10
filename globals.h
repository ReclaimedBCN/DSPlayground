#pragma once

#include <atomic>
#include <vector>
#include "ftxui/dom/elements.hpp"

// constants
constexpr std::size_t SAMPLERATE = 48000; // should be a sampleRate supported by RTaudio and your soundcard
constexpr std::size_t BUFFERFRAMES = 256; // number of frames per audio callback
constexpr std::size_t RECORDDURATION = 3; // number of seconds to record
constexpr std::size_t RECORDFRAMES = SAMPLERATE * RECORDDURATION; // number of frames to record
constexpr char PLUGINSOURCE[] = "plugin.cpp"; // .cpp source file path for plugin
constexpr char PLUGINPATH[] = "./build/plugins/libplugin.dylib"; // shared library file to load
constexpr short BYTETOBITS = 8;
constexpr short RECORDBITDEPTH = 16;
constexpr float INVSAMPLERATE = 1.f / SAMPLERATE;

constexpr float PI = 3.14159265358979323846f;
constexpr float TWOPI = PI + PI;
constexpr float HALFPI = PI / 2.f;
constexpr float QUARTPI = PI / 4.f;
constexpr float PISQUARED = PI * PI;
constexpr float INVPI = 1.f / PI;
constexpr float INV60 = 1.f / 60;

// -----------------------------------------------------------------------------
// Globals
// -----------------------------------------------------------------------------
struct Globals
{
    unsigned int writeHead = 0; // circular buffer write head
    std::atomic<bool> reloading = 0; // flag to prevent double reloads
    std::vector<float> circularOutput = std::vector<float>(RECORDFRAMES + BUFFERFRAMES, 0.f); // circular buffer for output frames, sized with 1 extra buffer
    std::vector<float> wavWriteFloats = std::vector<float>(RECORDFRAMES, 0.f);
};

// hold function pointers and state for hot loaded data from plugin.cpp
struct PluginModule 
{
    void* handle = nullptr;                 // dynamic library handle returned by dlopen()
    void* state = nullptr;                  // pointer to DSPState instance created by DSP module
    void* (*create)();                      // function pointer: createDSP()
    void (*destroy)(void*);                 // function pointer: destroyDSP()
    void (*process)(void*, float*, int);    // function pointer: processAudio() + floatOut + numFrames
};

// circular buffer for logging standard output
class LogBuffer
{
    public:
        void setNewLine(std::string text)
        {
            _logBuffer[_writeHead] = text; // add to circular buffer
            _writeHead = (_writeHead + 1) & _size - 1; // increment & wrap
        }
        std::string getLine(int index) 
        {
            if (index >= 0 && index <= _size) return _logBuffer[index]; 
            else return "";
        }
        int getWriteHead() { return _writeHead; }
        int getSize() { return _size; }
        ftxui::Element getMiniLog()
        {
            int size = 16;
            std::string logOut = ""; // clear
            for (int i=1; i<size; i++) 
            {
                int jump = size - i; // start from back of queue
                int readHead = (_writeHead - jump) & size - 1; // wrap
                logOut += this->getLine(readHead) + "\n"; // output all lines, oldest --> most recent
            }
            return ftxui::paragraph(logOut);
        }
        ftxui::Element getFullLog()
        {
            std::string logOut = ""; // clear
            for (int i=1; i<_size; i++) 
            {
                int jump = _size - i; // start from back of queue
                int readHead = (_writeHead - jump) & _size - 1; // wrap
                logOut += this->getLine(readHead) + "\n"; // output all lines, oldest --> most recent 
            }
            return ftxui::paragraph(logOut);
        }
    private:
        int _size = 128;
        int _writeHead = 0;
        std::vector<std::string> _logBuffer = std::vector<std::string>(_size);
};

struct UiParams
{
    float freq = 220.f;
    float gain = 0.1f;
    float phase = 0.f;
    bool bypass = 0;
};
