// Copyright 2026 Reclaimed BCN. All rights reserved.
// Use of this source code is governed by the license found in the LICENSE file.

// ----------------------------------------------------------------------------------------------
// Hot-Reloadable DSP Code. Rebuild with command: make plugin
// ----------------------------------------------------------------------------------------------
#include "plugin.h"

// ----------------------------------------------------------------------------------------------
// Allocates a new PluginState object on the heap & returns a void* pointer to it
    // Called when the module is first loaded
    // extern "C" to prevent stripping of symbol names
// ----------------------------------------------------------------------------------------------
extern "C" void* createPlugin(void* uiParamsPoint) { return new PluginState(uiParamsPoint); }

// Frees the memory allocated in createPlugin()
    // Called when the module is about to be unloaded (i.e. before hot-reload)
extern "C" void destroyPlugin(void* state) { delete static_cast<PluginState*>(state); }

// DSP Code: Generates 'numFrames' samples into the 'out' buffer
    // Called once per audio block by host
extern "C" void processPlugin(void* state, float* out, int numFrames) 
{
    // Cast untyped void* back into a PluginState pointer
    PluginState* plugin = static_cast<PluginState*>(state);
    plugin->process(out, numFrames);
}
