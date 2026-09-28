#include "mfd_gaze_tracker.h"
#include <algorithm>
#include <cmath>

namespace edvr::mfd {

namespace {
    constexpr float kRadToDeg = 180.0f / 3.14159265358979323846f;
}

MfdGazeTracker::MfdGazeTracker() = default;

float MfdGazeTracker::computeGazeAngle(const Vec3& headPos, const Vec3& headForward,
                                     const Vec3& mfdCenter) {
    Vec3 toMfd = (mfdCenter - headPos).normalized();
    float dotVal = std::clamp(headForward.normalized().dot(toMfd), -1.0f, 1.0f);
    return std::acos(dotVal) * kRadToDeg;
}

bool MfdGazeTracker::testRayIntersection(const Vec3& headPos, const Vec3& headDir,
                                        const MfdPose& mfdPose, float* outDistance,
                                        float anchorOffsetX, float anchorOffsetY,
                                        float hitMargin) {
    // Normal facing the pilot (in local MFD space, +Z faces toward viewer)
    Vec3 normal = mfdPose.orientation.rotate(Vec3(0, 0, 1.0f));

    float denom = headDir.dot(normal);
    // Ray must be pointing toward the front surface of the display
    if (denom >= -1e-4f) return false;

    Vec3 p0 = mfdPose.position - headPos;
    float t = p0.dot(normal) / denom;
    if (t < 0.05f || t > 5.0f) return false; // Cockpit distances (5cm to 5m)

    if (outDistance) *outDistance = t;

    // Intersection point on the plane
    Vec3 hitPoint = headPos + (headDir * t);
    Vec3 localHit = hitPoint - mfdPose.position;

    // Project onto MFD local right and up axes
    Vec3 localRight = mfdPose.orientation.rotate(Vec3(1.0f, 0, 0));
    Vec3 localUp = mfdPose.orientation.rotate(Vec3(0, 1.0f, 0));

    float halfW = mfdPose.widthM * 0.5f;
    float halfH = mfdPose.heightM * 0.5f;

    // Offset the center of focus if configured (e.g. anchor focus to lower part of screen)
    float projX = localHit.dot(localRight) - (anchorOffsetX * halfW);
    float projY = localHit.dot(localUp) - (anchorOffsetY * halfH);

    float effectiveMargin = (std::max)(0.5f, hitMargin);
    return (std::abs(projX) <= halfW * effectiveMargin && std::abs(projY) <= halfH * effectiveMargin);
}

MfdFocusState MfdGazeTracker::update(const Vec3& headPos, const Vec3& headForward,
                                    const MfdPose& mfdPose, float dtSeconds) {
    // Offset target center if configured
    Vec3 localRight = mfdPose.orientation.rotate(Vec3(1.0f, 0, 0));
    Vec3 localUp = mfdPose.orientation.rotate(Vec3(0, 1.0f, 0));
    Vec3 anchorPos = mfdPose.position +
                     (localRight * (m_anchorOffsetX * mfdPose.widthM * 0.5f)) +
                     (localUp * (m_anchorOffsetY * mfdPose.heightM * 0.5f));

    m_lastGazeAngleDeg = computeGazeAngle(headPos, headForward, anchorPos);

    // Gaze is considered on-target when head ray intersects physical MFD surface bounds
    bool onTarget = testRayIntersection(headPos, headForward, mfdPose, nullptr,
                                        m_anchorOffsetX, m_anchorOffsetY, m_hitMargin);

    switch (m_state) {
        case MfdFocusState::kUnfocused:
            if (onTarget) {
                m_state = MfdFocusState::kAcquiring;
                m_timer = 0.0f;
            }
            break;

        case MfdFocusState::kAcquiring:
            if (onTarget) {
                m_timer += dtSeconds;
                if (m_timer >= m_dwellEnterTime) {
                    m_state = MfdFocusState::kFocused;
                    m_timer = 0.0f;
                }
            } else {
                // Glanced away before dwell threshold: abort acquisition
                m_state = MfdFocusState::kUnfocused;
                m_timer = 0.0f;
            }
            break;

        case MfdFocusState::kFocused:
            if (!onTarget) {
                // Gaze drifted off MFD: enter release hysteresis grace period
                m_state = MfdFocusState::kReleasing;
                m_timer = 0.0f;
            }
            break;

        case MfdFocusState::kReleasing:
            if (onTarget) {
                // Re-acquired before grace period elapsed: stay focused
                m_state = MfdFocusState::kFocused;
                m_timer = 0.0f;
            } else {
                m_timer += dtSeconds;
                if (m_timer >= m_dwellExitTime) {
                    m_state = MfdFocusState::kUnfocused;
                    m_timer = 0.0f;
                }
            }
            break;
    }

    return m_state;
}

} // namespace edvr::mfd
