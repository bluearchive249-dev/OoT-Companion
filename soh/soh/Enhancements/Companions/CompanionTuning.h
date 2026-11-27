#pragma once

// Copyright © 2026 Grimsfrost. All rights reserved.
// Companions: numbers read at runtime from companion_tuning.json in the game's folder (or mods/), so distances,
// speeds and timings can be changed without rebuilding. Reloaded when the file changes. A key that isn't in the file
// keeps its built-in default. Every key asked for is listed, with the value in use, in companion_tuning_keys.json.

#ifdef __cplusplus
extern "C" {
#endif

// A number: "key": 1.5
float CompTune(const char* key, float def);
// One entry of a list: "key": [3, 4, 5]
float CompTuneAt(const char* key, int index, float def);

#ifdef __cplusplus
}
#endif
