#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <vector>
// Pointer art for the whole app. One picture drives both the native cursor of the
// fullscreen surfaces and the CSS cursor of every HTML window, so they always match.
// Everything except orbit() needs GDI+ to be started by the caller.
namespace CursorArt {
struct Image {
    int width = 0, height = 0, hotX = 0, hotY = 0;
    std::vector<uint32_t> pixels;        // straight alpha 0xAARRGGBB, top row first
    bool empty() const { return pixels.empty(); }
};
// Orbit tint as 0xRRGGBB, in the same order as the clock colours.
uint32_t tint(int index);
std::string hex(uint32_t rgb);           // "#rrggbb"
// Edge of the three size steps in pixels at 100% scaling: 24, 28, 32. The HTML side
// keeps cursors at or under 32 px, the largest Chromium draws right up to a window edge.
int baseSize(int step);
// The built-in osu!-style pointer: soft glow, bright ring with a dark contrast rim, centre dot.
Image orbit(int size, uint32_t rgb);
// A user's .png, .cur or .ani drawn into a size x size square. PNG files take their click
// point from `centerTip` (centre or top-left); cursor files carry their own. Animated
// cursors give their first frame here.
bool loadFile(const std::wstring& path, int size, bool centerTip, Image& out);
// Native cursor from an image. The caller owns the handle and frees it with DestroyCursor.
HCURSOR toCursor(const Image& image);
// Native cursor straight from a file, keeping .ani animation. Caller owns the handle.
HCURSOR loadNative(const std::wstring& path, int size, bool centerTip);
// The image as a data: URL, or an empty string on failure.
std::string pngDataUrl(const Image& image);
// True for the file types loadFile understands.
bool supportedFile(const std::wstring& path);
}
