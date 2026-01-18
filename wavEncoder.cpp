// Copyright 2026 Reclaimed BCN. All rights reserved.
// Use of this source code is governed by the license found in the LICENSE file.

// thanks to @Thrifleganger https://github.com/Thrifleganger/audio-programming-youtube

#include <iostream>
#include <cmath>
#include <fstream>
#include <string>

#include "globals.h"

// for writing a specific number of bytes independent of system's int implementation
void writeBytes(std::ofstream& file, int value, int size) 
{
    file.write(reinterpret_cast<const char*> (&value), size);
}

void writeWav(Globals& globals, LogBuffer& logBuff) {
    logBuff.setNewLine("recording..");
    // setup
    std::ofstream audioFile;
    audioFile.open("recording.wav", std::ios::binary);

    // header chunk
    audioFile << "RIFF";
    audioFile << "----"; // size of wav file minus 8 bytes (excluding RIFF id and size value)
    audioFile << "WAVE";

    // format chunk
    audioFile << "fmt ";
    writeBytes(audioFile, 16, 4); // Size
    writeBytes(audioFile, 1, 2); // Compression code
    writeBytes(audioFile, 1, 2); // Number of channels
    writeBytes(audioFile, SAMPLERATE, 4); // Sample rate
    writeBytes(audioFile, SAMPLERATE * RECORDBITDEPTH  / BYTETOBITS, 4 ); // Byte rate
    writeBytes(audioFile, RECORDBITDEPTH / 8, 2); // Block align
    writeBytes(audioFile, RECORDBITDEPTH , 2); // Bit depth

    // data chunk
    audioFile << "data";
    audioFile << "----"; // size of audio data

    int preAudioPosition = audioFile.tellp();

    // SAMPLE WRITING
        // scale float samples to unsigned int for writing to .wav file
    auto maxAmplitude = pow(2, RECORDBITDEPTH  - 1) - 1;
        // circular buffer readHead, 1 buffer ahead of writeHead
    std::size_t readHead = (globals.writeHead.load() + BUFFERFRAMES) % globals.circularOutput.size();
    for(int i = 0; i < RECORDFRAMES; i++ ) 
    {
        globals.wavWriteFloats[i] = globals.circularOutput[readHead];
        readHead = (readHead + 1) % globals.circularOutput.size();

        float sample = globals.wavWriteFloats[i];
        int intSample = static_cast<int>(sample * maxAmplitude);
        writeBytes(audioFile, intSample, 2);
    }

    int postAudioPosition = audioFile.tellp();

    // replace file size placeholders
        // audio data size
    audioFile.seekp(preAudioPosition - 4);
    writeBytes(audioFile, postAudioPosition - preAudioPosition, 4);
        // whole wav size
    audioFile.seekp(4, std::ios::beg);
    writeBytes(audioFile, postAudioPosition - 8, 4);

    audioFile.close();

    logBuff.setNewLine("recording saved!");
    logBuff.setNewLine("recording.wav = " + std::to_string(RECORDDURATION) + " seconds");
}

