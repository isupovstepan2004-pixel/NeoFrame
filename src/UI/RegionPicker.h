#pragma once
#include <Windows.h>

namespace nf {

// Full-virtual-screen dim overlay: drag to select a rectangle, Esc to cancel.
// Returns true and fills `screenRect` (virtual-screen pixels) on success.
bool PickScreenRegion(HINSTANCE inst, RECT& screenRect);

} // namespace nf
