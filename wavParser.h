#pragma once

#include <fstream>
#include "globals.h"

void writeBytes(std::ofstream& file, int value, int size);
void writeWav(Globals& globals, LogBuffer& logBuff);
