// Copyright 2026 Reclaimed BCN. All rights reserved.
// Use of this source code is governed by the license found in the LICENSE file.

// ----------------------------------------------------------------------------------------------
// Hot-reloadable DSP Plugin. Rebuild with command: make plugin
// ----------------------------------------------------------------------------------------------
#include "plugin.h"

// allocates PluginState object on heap & returns a void* pointer to it
    // called when the module is first loaded
    // extern "C" to prevent stripping of symbol names
extern "C" void* createPlugin(void* uiParamsPoint) { return new PluginState(uiParamsPoint); }

// frees the memory allocated by createPlugin()
    // called when the module is about to be unloaded (before hot-reload)
extern "C" void destroyPlugin(void* state) { delete static_cast<PluginState*>(state); }

// generates 'numFrames' samples into 'out' buffer, called once per block by host
extern "C" void processPlugin(void* state, float* out, int numFrames) 
{
    PluginState* plugin = static_cast<PluginState*>(state);
    plugin->process(out, numFrames);
}
