#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace edvr::mfd {

// Layout template a screen renders with. #11 adds compact_banner.
enum class MfdLayoutTemplate : uint8_t {
    kStandard = 0
};

struct MfdRenderSize {
    int width = 0;
    int height = 0;

    bool operator==(const MfdRenderSize& o) const { return width == o.width && height == o.height; }
    bool operator!=(const MfdRenderSize& o) const { return !(*this == o); }
};

// Pixel density shared by every screen: the original 512 px across 0.24 m (~2133 px/m),
// so texture aspect always matches the quad's physical aspect.
constexpr float kPixelsPerMeter = 512.0f / 0.24f;
constexpr int kMinRenderDim = 64;
constexpr int kMaxRenderDim = 2048;

// User Width/Height factors (SETTINGS tab), same range and step as Display Scale.
constexpr float kMinAxisFactor = 0.50f;
constexpr float kMaxAxisFactor = 2.0f;
constexpr float kAxisFactorStep = 0.05f;

// A manifest override whose aspect differs from widthM:heightM by more than this looks stretched.
constexpr float kAspectMismatchTolerance = 0.02f;

inline int clampRenderDim(float px) {
    return (std::clamp)(static_cast<int>(std::lround(px)), kMinRenderDim, kMaxRenderDim);
}

// Render size before the user's Width/Height factors: the manifest's renderWidth/renderHeight
// when both are declared, otherwise derived from the physical size at kPixelsPerMeter.
inline MfdRenderSize baseRenderSize(float widthM, float heightM, int overrideWidth = 0, int overrideHeight = 0) {
    if (overrideWidth > 0 && overrideHeight > 0) {
        return { clampRenderDim(static_cast<float>(overrideWidth)), clampRenderDim(static_cast<float>(overrideHeight)) };
    }
    return { clampRenderDim(widthM * kPixelsPerMeter), clampRenderDim(heightM * kPixelsPerMeter) };
}

// Width/Height factors grow the texture along with the quad, so a wider screen shows more
// columns instead of stretching texels. Display Scale is deliberately not an input here.
inline MfdRenderSize effectiveRenderSize(MfdRenderSize base, float widthFactor, float heightFactor) {
    return { clampRenderDim(base.width * widthFactor), clampRenderDim(base.height * heightFactor) };
}

inline bool renderAspectMismatch(int renderWidth, int renderHeight, float widthM, float heightM) {
    if (renderWidth <= 0 || renderHeight <= 0 || widthM <= 0.0f || heightM <= 0.0f) return false;
    float ratio = (static_cast<float>(renderWidth) / renderHeight) / (widthM / heightM);
    return std::abs(ratio - 1.0f) > kAspectMismatchTolerance;
}

// Smallest render height at which a template still shows its minimum content.
// Standard: header + tab bar (72 px), footer (34 px) and one list row with sublabel (36 px).
inline int minContentHeightPx(MfdLayoutTemplate layout) {
    switch (layout) {
        case MfdLayoutTemplate::kStandard:
        default:
            return 72 + 34 + 36;
    }
}

// Lowest Height factor that keeps the template's minimum content. Never above 1.0: an
// author's declared size always stands, the user just can't shrink it further.
inline float minHeightFactor(MfdLayoutTemplate layout, int baseRenderHeight) {
    if (baseRenderHeight <= 0) return kMinAxisFactor;
    float f = static_cast<float>(minContentHeightPx(layout)) / static_cast<float>(baseRenderHeight);
    return (std::clamp)(f, kMinAxisFactor, 1.0f);
}

// Snap accumulated float steps to the 0.01 grid the INI and SETTINGS tab display.
inline float roundFactor(float f) {
    return std::round(f * 100.0f) / 100.0f;
}

} // namespace edvr::mfd
