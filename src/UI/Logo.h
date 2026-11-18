#pragma once
#include <Windows.h>
#include <d2d1.h>

namespace nf {

// NeoFrame emblem: a "ghost trail" of three frames (the last one real, the earlier
// ones fading) on a soft blue-to-indigo rounded tile.
void DrawLogo(ID2D1RenderTarget* rt, float x, float y, float size);

// Renders the emblem into an HICON (used for the window / taskbar icon).
HICON CreateLogoIcon(ID2D1Factory* factory, int size);

} // namespace nf
