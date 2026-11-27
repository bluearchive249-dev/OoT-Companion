#pragma once

// Copyright © 2026 Grimsfrost. All rights reserved.
// Companions: Saria, Ruto, Malon and Zelda travel with Link (see Companions.cpp)

#ifdef __cplusplus
#include <memory>
namespace Ship {
class GuiWindow;
}
// The speech bubbles over their heads
std::shared_ptr<Ship::GuiWindow> Companions_CreateHudWindow();
#endif
