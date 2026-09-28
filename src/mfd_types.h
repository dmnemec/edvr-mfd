#pragma once

#include <cstdint>
#include <string>
#include <cmath>

namespace edvr::mfd {

// 3D vector for cockpit positions and directions in meters.
struct Vec3 {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;

    constexpr Vec3() = default;
    constexpr Vec3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {}

    Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
    Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
    Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }

    float lengthSq() const { return x * x + y * y + z * z; }
    float length() const { return std::sqrt(lengthSq()); }

    Vec3 normalized() const {
        float l = length();
        return l > 1e-6f ? Vec3(x / l, y / l, z / l) : Vec3(0, 0, 0);
    }

    float dot(const Vec3& o) const { return x * o.x + y * o.y + z * o.z; }
};

// Quaternion for 3D orientation in seated tracking space.
struct Quat {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;

    constexpr Quat() = default;
    constexpr Quat(float x_, float y_, float z_, float w_) : x(x_), y(y_), z(z_), w(w_) {}

    // Construct from Euler angles in degrees (pitch=X, yaw=Y, roll=Z).
    static Quat fromEulerDegrees(float pitchDeg, float yawDeg, float rollDeg) {
        constexpr float kDegToRad = 3.14159265358979323846f / 180.0f;
        float p = pitchDeg * kDegToRad * 0.5f;
        float y = yawDeg * kDegToRad * 0.5f;
        float r = rollDeg * kDegToRad * 0.5f;

        float sinP = std::sin(p), cosP = std::cos(p);
        float sinY = std::sin(y), cosY = std::cos(y);
        float sinR = std::sin(r), cosR = std::cos(r);

        return {
            sinP * cosY * cosR - cosP * sinY * sinR,
            cosP * sinY * cosR + sinP * cosY * sinR,
            cosP * cosY * sinR - sinP * sinY * cosR,
            cosP * cosY * cosR + sinP * sinY * sinR
        };
    }

    // Rotate a vector by this quaternion.
    Vec3 rotate(const Vec3& v) const {
        // v' = q * (0, v) * q^-1
        Vec3 qv(x, y, z);
        Vec3 uv = Vec3(
            qv.y * v.z - qv.z * v.y,
            qv.z * v.x - qv.x * v.z,
            qv.x * v.y - qv.y * v.x
        );
        Vec3 uuv = Vec3(
            qv.y * uv.z - qv.z * uv.y,
            qv.z * uv.x - qv.x * uv.z,
            qv.x * uv.y - qv.y * uv.x
        );
        return v + (uv * (2.0f * w)) + (uuv * 2.0f);
    }
};

// 3D pose of an MFD panel in seated tracking space.
struct MfdPose {
    Vec3 position;           // Coordinates in meters relative to seated head origin
    Quat orientation;        // Rotation relative to cockpit forward
    float widthM = 0.24f;    // Physical width in meters (e.g. 24 cm)
    float heightM = 0.16f;   // Physical height in meters (e.g. 16 cm)
};

// Head-gaze focus lifecycle state.
enum class MfdFocusState : uint8_t {
    kUnfocused = 0,   // Pilot is looking elsewhere (normal flight controls)
    kAcquiring = 1,   // Gaze has entered MFD cone, awaiting dwell-time threshold
    kFocused = 2,     // Active focus established: HUD glows, UI controls intercepted
    kReleasing = 3    // Gaze left MFD cone, within exit hysteresis grace period
};

// Abstracted UI navigation actions mirror Elite Dangerous cockpit panel navigation.
enum class MfdInputAction : uint16_t {
    kNone      = 0,
    kUp        = 1 << 0,  // Move selection up / scroll up
    kDown      = 1 << 1,  // Move selection down / scroll down
    kLeft      = 1 << 2,  // Adjust value left / previous column
    kRight     = 1 << 3,  // Adjust value right / next column
    kSelect    = 1 << 4,  // Activate selected item / copy to clipboard
    kBack      = 1 << 5,  // Return to parent view / cancel
    kNextTab   = 1 << 6,  // Cycle to next panel tab (Q/E or LB/RB)
    kPrevTab   = 1 << 7   // Cycle to previous panel tab
};

inline MfdInputAction operator|(MfdInputAction a, MfdInputAction b) {
    return static_cast<MfdInputAction>(static_cast<uint16_t>(a) | static_cast<uint16_t>(b));
}

inline bool hasAction(MfdInputAction mask, MfdInputAction flag) {
    return (static_cast<uint16_t>(mask) & static_cast<uint16_t>(flag)) != 0;
}

// Provider strategy type:
// - Option A: Declarative JSON state schema (active)
// - Option B: Scripted / programmable code plugin (reserved architecture slot)
enum class MfdProviderType : uint8_t {
    kDeclarativeJson = 0,  // Option A: Declarative JSON data loaded via REST/WebSocket/files
    kScriptedCode    = 1   // Option B: Reserved slot for Lua/WASM/C++ code plugins
};

// Compositor display backend strategy:
// - Option A: OpenXR Quad Composition Layer (active)
// - Option B: D3D11 In-Game Mesh Injection (reserved architecture slot)
enum class MfdCompositorBackend : uint8_t {
    kOpenXrQuadLayer = 0,  // Option A: Hardware-composited quad in OpenXR local space
    kD3D11SceneMesh  = 1   // Option B: Reserved slot for D3D11 scene draw injection
};

// Bitmask for contextual activity gating (Status.json Flags bit checking)
enum MfdActivityMask : uint32_t {
    kActivityAlways       = 0,
    kActivityShip         = 1 << 0,  // Active in Ship (Flags bit 27)
    kActivitySrv          = 1 << 1,  // Active in SRV (Flags bit 26)
    kActivityFighter      = 1 << 2,  // Active in SLF Fighter (Flags bit 28)
    kActivityOnFoot       = 1 << 3,  // Active On Foot
    kActivityInFlight     = 1 << 4,  // Active only in Flight (not docked or landed)
    kActivityDocked       = 1 << 5,  // Active when docked (Flags bit 0)
    kActivityHardpoints   = 1 << 6,  // Active when hardpoints deployed (Flags bit 6)
    kActivityCombat       = 1 << 7,  // Active in Combat Mode (Flags bit 27 == 0)
    kActivityAnalysis     = 1 << 8,  // Active in Analysis Mode (Flags bit 27 == 1 / 0x08000000)
};

// Color theme presets
enum class MfdColorTheme : uint8_t {
    kDefaultAmber = 0,   // Classic Orange HUD
    kCyanIce      = 1,   // Ice Blue / Cyan
    kMatrixGreen  = 2,   // Emerald / Matrix Green
    kSolarWhite   = 3,   // High-Contrast White
    kCrimson      = 4,   // Combat Red / Crimson
    kPurpleHaze   = 5    // Violet / Amethyst
};

// Color representation for HUD rendering (RGBA8).
struct MfdColor {
    uint8_t r = 0;
    uint8_t g = 0;
    uint8_t b = 0;
    uint8_t a = 255;

    constexpr MfdColor() = default;
    constexpr MfdColor(uint8_t r_, uint8_t g_, uint8_t b_, uint8_t a_ = 255)
        : r(r_), g(g_), b(b_), a(a_) {}

    uint32_t toRgba() const {
        return (static_cast<uint32_t>(a) << 24) |
               (static_cast<uint32_t>(b) << 16) |
               (static_cast<uint32_t>(g) << 8)  |
               static_cast<uint32_t>(r);
    }

    constexpr bool operator==(const MfdColor& o) const {
        return r == o.r && g == o.g && b == o.b && a == o.a;
    }

    constexpr bool operator!=(const MfdColor& o) const {
        return !(*this == o);
    }
};

struct MfdPaletteSet {
    MfdColor mainColor;
    MfdColor dimColor;
    MfdColor brightColor;
    MfdColor accentColor;
    MfdColor borderColor;
    MfdColor bgColor;
};

// Signature Elite Dangerous HUD color palette.
namespace palette {
    constexpr MfdColor kAmberNormal(255, 110, 0, 255);    // Signature orange HUD
    constexpr MfdColor kAmberBright(255, 150, 0, 255);   // Glowing selected text
    constexpr MfdColor kAmberDim(160, 60, 0, 255);        // Unfocused / disabled text
    constexpr MfdColor kCyanAccent(0, 190, 255, 255);     // Target / informational blue
    constexpr MfdColor kAlertRed(255, 30, 30, 255);       // Danger / warning red
    constexpr MfdColor kSuccessGreen(0, 240, 60, 255);    // Confirmed / active green
    constexpr MfdColor kBackground(10, 8, 6, 255);        // Cockpit display backing
    constexpr MfdColor kFrameBorder(255, 110, 0, 255);    // Bezel border

    inline MfdPaletteSet getTheme(MfdColorTheme theme) {
        switch (theme) {
            case MfdColorTheme::kCyanIce:
                return {
                    MfdColor(0, 180, 255), MfdColor(0, 85, 130), MfdColor(70, 220, 255),
                    MfdColor(255, 150, 0), MfdColor(0, 200, 255), MfdColor(6, 12, 18)
                };
            case MfdColorTheme::kMatrixGreen:
                return {
                    MfdColor(50, 230, 80), MfdColor(0, 110, 20), MfdColor(50, 255, 90),
                    MfdColor(0, 190, 255), MfdColor(0, 255, 60), MfdColor(6, 16, 8)
                };
            case MfdColorTheme::kSolarWhite:
                return {
                    MfdColor(230, 235, 245), MfdColor(120, 125, 135), MfdColor(255, 255, 255),
                    MfdColor(0, 190, 255), MfdColor(240, 245, 255), MfdColor(10, 12, 16)
                };
            case MfdColorTheme::kCrimson:
                return {
                    MfdColor(255, 25, 25), MfdColor(130, 10, 10), MfdColor(255, 75, 75),
                    MfdColor(255, 150, 0), MfdColor(255, 35, 35), MfdColor(18, 8, 8)
                };
            case MfdColorTheme::kPurpleHaze:
                return {
                    MfdColor(195, 55, 255), MfdColor(95, 25, 130), MfdColor(220, 95, 255),
                    MfdColor(0, 190, 255), MfdColor(205, 65, 255), MfdColor(14, 8, 20)
                };
            case MfdColorTheme::kDefaultAmber:
            default:
                return {
                    kAmberNormal, kAmberDim, kAmberBright,
                    kCyanAccent, kFrameBorder, kBackground
                };
        }
    }
}

} // namespace edvr::mfd
