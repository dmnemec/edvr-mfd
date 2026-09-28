#pragma once

#include <windows.h>
#include <stdint.h>
#include <cmath>

namespace edvr::mfd {

struct MfdSharedState {
    volatile LONG initialized;
    volatile LONG draws;
    volatile LONG focusState;
    volatile LONG inFrustum;
    volatile LONG eyeX_mm;
    volatile LONG eyeY_mm;
    volatile LONG eyeZ_mm;
    volatile LONG screenX;
    volatile LONG screenY;
    volatile LONG screenW;
    volatile LONG screenH;
};

inline MfdSharedState* getMfdSharedState() {
    static MfdSharedState* s_shared = nullptr;
    if (s_shared) return s_shared;

    wchar_t name[64];
    swprintf_s(name, L"Local\\edvr_mfd_telemetry_v1_%lu", GetCurrentProcessId());
    HANDLE hMap = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(MfdSharedState), name);
    if (!hMap) return nullptr;

    s_shared = static_cast<MfdSharedState*>(MapViewOfFile(hMap, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(MfdSharedState)));
    return s_shared;
}

inline void publishSharedTelemetry(uint32_t draws, int focusState, bool inFrustum,
                                   float eyeX, float eyeY, float eyeZ,
                                   float screenX, float screenY, float screenW, float screenH) {
    MfdSharedState* s = getMfdSharedState();
    if (!s) return;
    InterlockedExchange(&s->initialized, 1);
    InterlockedExchange(&s->draws, static_cast<LONG>(draws));
    InterlockedExchange(&s->focusState, static_cast<LONG>(focusState));
    InterlockedExchange(&s->inFrustum, inFrustum ? 1 : 0);
    InterlockedExchange(&s->eyeX_mm, static_cast<LONG>(std::round(eyeX * 1000.0f)));
    InterlockedExchange(&s->eyeY_mm, static_cast<LONG>(std::round(eyeY * 1000.0f)));
    InterlockedExchange(&s->eyeZ_mm, static_cast<LONG>(std::round(eyeZ * 1000.0f)));
    InterlockedExchange(&s->screenX, static_cast<LONG>(std::round(screenX)));
    InterlockedExchange(&s->screenY, static_cast<LONG>(std::round(screenY)));
    InterlockedExchange(&s->screenW, static_cast<LONG>(std::round(screenW)));
    InterlockedExchange(&s->screenH, static_cast<LONG>(std::round(screenH)));
}

inline bool readSharedTelemetry(uint32_t* draws, int* focusState, bool* inFrustum,
                                float* eyeX, float* eyeY, float* eyeZ,
                                float* screenX = nullptr, float* screenY = nullptr,
                                float* screenW = nullptr, float* screenH = nullptr) {
    MfdSharedState* s = getMfdSharedState();
    if (!s || InterlockedCompareExchange(&s->initialized, 0, 0) == 0) return false;
    if (draws) *draws = static_cast<uint32_t>(InterlockedCompareExchange(&s->draws, 0, 0));
    if (focusState) *focusState = static_cast<int>(InterlockedCompareExchange(&s->focusState, 0, 0));
    if (inFrustum) *inFrustum = (InterlockedCompareExchange(&s->inFrustum, 0, 0) != 0);
    if (eyeX) *eyeX = static_cast<float>(InterlockedCompareExchange(&s->eyeX_mm, 0, 0)) / 1000.0f;
    if (eyeY) *eyeY = static_cast<float>(InterlockedCompareExchange(&s->eyeY_mm, 0, 0)) / 1000.0f;
    if (eyeZ) *eyeZ = static_cast<float>(InterlockedCompareExchange(&s->eyeZ_mm, 0, 0)) / 1000.0f;
    if (screenX) *screenX = static_cast<float>(InterlockedCompareExchange(&s->screenX, 0, 0));
    if (screenY) *screenY = static_cast<float>(InterlockedCompareExchange(&s->screenY, 0, 0));
    if (screenW) *screenW = static_cast<float>(InterlockedCompareExchange(&s->screenW, 0, 0));
    if (screenH) *screenH = static_cast<float>(InterlockedCompareExchange(&s->screenH, 0, 0));
    return true;
}

} // namespace edvr::mfd
