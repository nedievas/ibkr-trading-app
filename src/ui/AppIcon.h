#pragma once
// ============================================================================
// AppIcon.h — the app icon (design B: three candlesticks on a charcoal rounded
// square), drawn into an RGBA buffer at any size.
//
// Windows embeds resources/windows/app.ico instead (tools/gen_icon.py draws the
// same design). Linux sets this as the GLFW window icon and macOS as the Dock
// icon, since the app has no image decoder to load a PNG with. Geometry is in
// the same 1024-unit space as gen_icon.py, so both stay alike.
// ============================================================================

#include <algorithm>
#include <cstdint>
#include <vector>

namespace ui {

namespace detail {

struct IconRect { double x0, y0, x1, y1, r; uint8_t cr, cg, cb; };

inline bool InRoundedRect(const IconRect& q, double x, double y) {
    if (x < q.x0 || x > q.x1 || y < q.y0 || y > q.y1) return false;
    const double cx = std::clamp(x, q.x0 + q.r, q.x1 - q.r);
    const double cy = std::clamp(y, q.y0 + q.r, q.y1 - q.r);
    const double dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= q.r * q.r;
}

}  // namespace detail

// size x size pixels, row-major RGBA, top row first, alpha not premultiplied.
inline std::vector<uint8_t> DrawAppIconRGBA(int size) {
    using detail::IconRect;
    std::vector<uint8_t> px;
    if (size <= 0) return px;
    px.assign(static_cast<size_t>(size) * size * 4, 0);

    constexpr uint8_t kG[3] = { 38, 194, 110 };   // green candle
    constexpr uint8_t kR[3] = { 232, 72, 72 };    // red candle
    // One candle: wick (42 wide, from high to low), then a rounded body.
    auto candle = [&](double x, double lo, double hi, double o, double c,
                      const uint8_t* col, std::vector<IconRect>& out) {
        constexpr double w = 150.0, wick = 42.0;
        out.push_back({ x - wick / 2, hi, x + wick / 2, lo, 0.0, col[0], col[1], col[2] });
        out.push_back({ x - w / 2, std::min(o, c), x + w / 2, std::max(o, c), w * 0.12,
                        col[0], col[1], col[2] });
    };
    std::vector<IconRect> shapes;
    candle(300, 760, 380, 700, 470, kG, shapes);
    candle(512, 660, 300, 360, 600, kR, shapes);
    candle(724, 600, 190, 560, 250, kG, shapes);
    const IconRect bg { 24, 24, 1000, 1000, 200, 0, 0, 0 };

    constexpr int ss = 4;                          // 4x4 samples per pixel
    const double scale = 1024.0 / size;
    for (int py = 0; py < size; ++py) {
        for (int pxl = 0; pxl < size; ++pxl) {
            double r = 0, g = 0, b = 0, cover = 0;
            for (int sy = 0; sy < ss; ++sy) {
                for (int sx = 0; sx < ss; ++sx) {
                    const double x = (pxl + (sx + 0.5) / ss) * scale;
                    const double y = (py  + (sy + 0.5) / ss) * scale;
                    if (!detail::InRoundedRect(bg, x, y)) continue;
                    // Background: vertical gradient (40,44,54) -> (18,20,26).
                    const double t = std::clamp(y / 1023.0, 0.0, 1.0);
                    double cr = 40 + (18 - 40) * t, cg = 44 + (20 - 44) * t, cb = 54 + (26 - 54) * t;
                    for (const IconRect& q : shapes)
                        if (detail::InRoundedRect(q, x, y)) { cr = q.cr; cg = q.cg; cb = q.cb; }
                    r += cr; g += cg; b += cb; cover += 1;
                }
            }
            uint8_t* o = &px[(static_cast<size_t>(py) * size + pxl) * 4];
            if (cover > 0) {
                o[0] = static_cast<uint8_t>(r / cover + 0.5);
                o[1] = static_cast<uint8_t>(g / cover + 0.5);
                o[2] = static_cast<uint8_t>(b / cover + 0.5);
            }
            o[3] = static_cast<uint8_t>(255.0 * cover / (ss * ss) + 0.5);
        }
    }
    return px;
}

}  // namespace ui
