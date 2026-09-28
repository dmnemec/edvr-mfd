#pragma once

#include "mfd_types.h"
#include "mfd_view_model.h"
#include "mfd_provider.h"
#include "mfd_renderer.h"
#include "mfd_gaze_tracker.h"
#include "mfd_input_router.h"
#include "mfd_compositor.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <vector>

struct XrPosef;
struct XrFovf;
struct XrCompositionLayerBaseHeader;

namespace edvr::mfd {

// Representation of an in-cockpit MFD display slot.
struct MfdSlot {
    std::string name;
    MfdPose pose;
    MfdPose baselinePose; // Default rest pose
    float pitchDeg = 0.0f;
    float yawDeg = 0.0f;
    float scale = 1.0f;
    std::unique_ptr<IMfdProvider> provider;
    std::unique_ptr<MfdRenderer> renderer;
    MfdGazeTracker gazeTracker;
    bool isVisible = true;
    bool autoHideUntilGaze = false; // Invisible until gaze activated
    bool trackingLocked = true; // true: locked to cockpit 3D space, false: freemove HUD attached to head
    float opacity = 0.75f;
    MfdColorTheme theme = MfdColorTheme::kDefaultAmber;
    bool useCustomColor = false;
    MfdColor customColor{255, 110, 0, 255}; // Custom vector line color (RGBA)
    uint32_t activityMask = kActivityAlways; // Vehicle / state gating
    float currentAlpha = 1.0f; // Smooth fade transition for auto-hide
    float maxPerspectiveTilt = 25.0f; // Max angular deviation limit from baseline
};

// Central manager coordinating in-cockpit MFD displays, gaze tracking,
// input routing, and rendering.
class MfdManager {
public:
    static MfdManager& instance();

    MfdManager();
    ~MfdManager();

    // Lifecycle
    bool initialize(int renderWidth = 512, int renderHeight = 384);
    void shutdown();
    bool isEnabled() const;
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool loadSettings();
    bool saveSettings();

    // Slot management
    bool addSlot(std::string name, std::unique_ptr<IMfdProvider> provider, const MfdPose& pose);
    MfdSlot* findSlot(const std::string& name);
    const std::vector<MfdSlot>& slots() const { return m_slots; }
    size_t slotCount() const { return m_slots.size(); }

    // Currently focused slot (if any)
    MfdSlot* focusedSlot();

    // Per-frame update (called from VR render loop)
    void update(const Vec3& headPos, const Vec3& headForward, float dtSeconds);

    // Render all visible MFD slots
    void render();

    // Render projected MFD onto eye swapchain RTV in 3D cockpit space
    void renderToEyeRtv(ID3D11Device* device, ID3D11DeviceContext* context,
                        ID3D11RenderTargetView* rtv,
                        const XrPosef& eyePose, const XrFovf& eyeFov,
                        uint32_t viewportWidth, uint32_t viewportHeight,
                        ID3D11VertexShader* vs, ID3D11PixelShader* ps,
                        ID3D11SamplerState* sampler, ID3D11Buffer* constantsBuffer);

    // Layer header accessor for OpenXR frame boundary
    const XrCompositionLayerBaseHeader* getOpenXrLayerHeader();

    // Input routing
    MfdInputRouter& inputRouter() { return m_inputRouter; }

    // Debug & diagnostic stats
    struct DebugStats {
        float eyeLocalX = 0.0f, eyeLocalY = 0.0f, eyeLocalZ = 0.0f;
        float screenX = 0.0f, screenY = 0.0f, screenW = 0.0f, screenH = 0.0f;
        uint32_t renderDraws = 0;
        bool inFrustum = false;
    };
    const DebugStats& debugStats() const { return m_debugStats; }

    // Cross-DLL shared telemetry (between openvr_api.dll and d3d11.dll)
    static void publishSharedTelemetry(uint32_t draws, int focusState, bool inFrustum,
                                       float eyeX, float eyeY, float eyeZ,
                                       float screenX, float screenY, float screenW, float screenH);
    static bool readSharedTelemetry(uint32_t* draws, int* focusState, bool* inFrustum,
                                    float* eyeX, float* eyeY, float* eyeZ,
                                    float* screenX = nullptr, float* screenY = nullptr,
                                    float* screenW = nullptr, float* screenH = nullptr);

    // Compositor access (Option A: OpenXR Quad layer, Option B: D3D11 scene mesh)
    IMfdCompositor* compositor() { return m_compositor.get(); }
    void setCompositor(std::unique_ptr<IMfdCompositor> compositor) {
        m_compositor = std::move(compositor);
    }

private:
    bool m_enabled = false;
    int m_renderWidth = 512;
    int m_renderHeight = 384;
    DebugStats m_debugStats;

    Vec3 m_lastHeadPos{0.0f, 0.0f, 0.0f};
    Quat m_lastHeadRot{0.0f, 0.0f, 0.0f, 1.0f};

    std::vector<MfdSlot> m_slots;
    MfdInputRouter m_inputRouter;
    std::unique_ptr<IMfdCompositor> m_compositor;
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_quadVs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_quadPs;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_quadCb;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_quadSampler;

    void ensureDefaultSlots();
};

} // namespace edvr::mfd
