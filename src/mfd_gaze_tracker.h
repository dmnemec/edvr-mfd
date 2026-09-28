#pragma once

#include "mfd_types.h"
#include <algorithm>

namespace edvr::mfd {

// Tracks the pilot's head gaze relative to an MFD in seated tracking space.
// Implements angular/frustum ray intersection and hysteresis dwell-time filtering.
class MfdGazeTracker {
public:
    MfdGazeTracker();

    // Configuration parameters:
    void setDwellEnterTime(float seconds) { m_dwellEnterTime = seconds; }
    void setDwellExitTime(float seconds) { m_dwellExitTime = seconds; }
    void setDwellTimeThreshold(float seconds) { m_dwellEnterTime = seconds; }
    void setConeAngleDegrees(float degrees) { m_coneAngleDegrees = degrees; }
    void setHitMargin(float margin) { m_hitMargin = margin; }
    void setAnchorOffset(float normX, float normY) { m_anchorOffsetX = normX; m_anchorOffsetY = normY; }

    float dwellEnterTime() const { return m_dwellEnterTime; }
    float dwellTimeThreshold() const { return m_dwellEnterTime; }
    float dwellExitTime() const { return m_dwellExitTime; }
    float coneAngleDegrees() const { return m_coneAngleDegrees; }
    float hitMargin() const { return m_hitMargin; }
    float anchorOffsetX() const { return m_anchorOffsetX; }
    float anchorOffsetY() const { return m_anchorOffsetY; }
    float dwellProgress() const {
        if (m_state == MfdFocusState::kAcquiring && m_dwellEnterTime > 0.001f) {
            return (std::clamp)(m_timer / m_dwellEnterTime, 0.0f, 1.0f);
        }
        return (m_state == MfdFocusState::kFocused) ? 1.0f : 0.0f;
    }

    // Evaluates gaze on each frame.
    // headPos: Head position in seated space (meters)
    // headForward: Normalized forward vector of the HMD (Z-forward or -Z depending on convention)
    // mfdPose: Position, orientation, and dimensions of the target MFD
    // dtSeconds: Time delta since previous frame
    MfdFocusState update(const Vec3& headPos, const Vec3& headForward,
                         const MfdPose& mfdPose, float dtSeconds);

    MfdFocusState currentState() const { return m_state; }
    bool isFocused() const { return m_state == MfdFocusState::kFocused; }
    float gazeAngleDegrees() const { return m_lastGazeAngleDeg; }

    // Direct ray-plane intersection test:
    // Returns true if ray from head intersects the physical MFD rectangular surface.
    static bool testRayIntersection(const Vec3& headPos, const Vec3& headDir,
                                    const MfdPose& mfdPose, float* outDistance = nullptr,
                                    float anchorOffsetX = 0.0f, float anchorOffsetY = 0.0f,
                                    float hitMargin = 1.05f);

    // Angular test:
    // Returns angle in degrees between head forward and direction vector to MFD center.
    static float computeGazeAngle(const Vec3& headPos, const Vec3& headForward,
                                 const Vec3& mfdCenter);

private:
    MfdFocusState m_state = MfdFocusState::kUnfocused;
    float m_dwellEnterTime = 0.15f;   // Time (s) gaze must dwell on MFD to trigger focus
    float m_dwellExitTime = 0.20f;    // Grace period (s) before focus is released
    float m_coneAngleDegrees = 8.0f;  // Tight angular threshold for focus acquisition
    float m_hitMargin = 1.05f;        // 5% margin around display bounds (tight, crisp acquisition)
    float m_anchorOffsetX = 0.0f;     // Normalized focus point offset (-1 to +1)
    float m_anchorOffsetY = 0.0f;     // E.g. -0.3 anchors gaze toward lower third of screen
    float m_timer = 0.0f;
    float m_lastGazeAngleDeg = 180.0f;
};

} // namespace edvr::mfd
