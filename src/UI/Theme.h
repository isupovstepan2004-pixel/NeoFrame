#pragma once
#include <d2d1.h>
#include <cstdint>

namespace nf::theme {

constexpr D2D1_COLOR_F Rgb(uint32_t hex, float a = 1.0f) {
    return D2D1_COLOR_F{ ((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                         (hex & 0xFF) / 255.0f, a };
}

inline constexpr auto kBg        = Rgb(0x171717);
inline constexpr auto kBg2       = Rgb(0x202020);
inline constexpr auto kCard      = Rgb(0x252525);
inline constexpr auto kCardHover = Rgb(0x2C2C2C);
inline constexpr auto kBorder    = Rgb(0x353535);
inline constexpr auto kAccent    = Rgb(0x60A5FA);
inline constexpr auto kAccent2   = Rgb(0x8B9CF6);
inline constexpr auto kText      = Rgb(0xF5F5F5);
inline constexpr auto kText2     = Rgb(0xA1A1AA);

inline constexpr float kRadius = 12.0f;  // card corner radius (DIP)
inline constexpr float kSidebarW = 220.0f;
inline constexpr float kPad = 32.0f;
inline constexpr float kGap = 16.0f;

} // namespace nf::theme
