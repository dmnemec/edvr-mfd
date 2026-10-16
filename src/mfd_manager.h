#pragma once

#include "mfd_types.h"
#include "mfd_view_model.h"
#include "mfd_provider.h"
#include "mfd_renderer.h"
#include "mfd_gaze_tracker.h"
#include "mfd_input_router.h"
#include "mfd_compositor.h"
#include "mfd_package.h"
#include "mfd_content_watcher.h"
#include "mfd_context.h"
#include "mfd_event_log.h"

#include <d3d11.h>
#include <wrl/client.h>
#include <memory>
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

struct XrPosef;
struct XrFovf;
struct XrCompositionLayerBaseHeader;

namespace edvr::mfd {

// Representation of an in-cockpit MFD display screen (Tickets A6, A7, A9, A12).
struct MfdScreen {
    std::string packageId;                   // Package identifier (e.g. "route", "ship")
    std::string screenId;                    // Unique screen ID within package (e.g. "route", "modules")
    std::string qualifiedId;                 // "<packageId>/<screenId>" (or legacy name if standalone)
    std::string name;                        // Legacy alias, same as qualifiedId or screenId
    std::string title;                       // Screen title displayed in headers/menus
    std::vector<std::string> contexts;       // Contexts where screen is visible (§7.3)
    std::vector<std::string> excludeContexts;// Excluded contexts (§7.3)
    bool autoHideUntilGaze = false;          // Gaze fade toggle
    bool hasExplicitPose = false;            // Whether pose was explicitly declared in manifest

    MfdPose pose;                            // Current 3D pose in cockpit space
    MfdPose baselinePose;                    // Default rest pose
    float pitchDeg = 0.0f;
    float yawDeg = 0.0f;
    float scale = 1.0f;
    float baseWidthM = 0.24f;
    float baseHeightM = 0.16f;

    std::unique_ptr<IMfdProvider> provider;
    std::unique_ptr<MfdRenderer> renderer;
    MfdGazeTracker gazeTracker;

    bool isVisible = true;                   // User/package show toggle
    bool isContextVisible = true;            // Evaluated by ContextEvaluator
    bool isCapSuppressed = false;            // Suppressed by 16-screen hard cap (§10.2)
    bool trackingLocked = true;              // Cockpit-locked vs head-locked
    float opacity = 0.75f;
    MfdColorTheme theme = MfdColorTheme::kDefaultAmber;
    bool useCustomColor = false;
    MfdColor customColor{255, 110, 0, 255};
    float currentAlpha = 1.0f;
    float maxPerspectiveTilt = 25.0f;
    uint32_t activityMask = 0xFFFFFFFF; // Legacy bitmask (deprecated, for backward compatibility)

    // ContentWatcher tracking (Ticket A3)
    uint64_t contentGeneration = 0;

    // Dirty-flag tracking & lazy GPU uploads (Ticket A9)
    bool isDirty = true;
    bool needsGpuUpload = true;
    uint64_t lastRenderedGeneration = 0;
    bool lastFocused = false;
    int lastSelectedTab = -1;
    int lastSelectedIndex = -1;
    MfdColorTheme lastTheme = MfdColorTheme::kDefaultAmber;
    std::string lastBadgeText;
    float lastRenderedAlpha = 1.0f;
};

// Typedef alias for backwards compatibility with earlier slot terminology
using MfdSlot = MfdScreen;

// Central manager coordinating in-cockpit MFD displays, gaze tracking,
// input routing, live content updates, context gating, and rendering.
class MfdManager {
public:
    static constexpr size_t kMaxVisibleScreens = 16; // Hard cap (§10.2)

    static MfdManager& instance();

    MfdManager();
    ~MfdManager();

    // Lifecycle
    bool initialize(int renderWidth = 512, int renderHeight = 384, const std::wstring& mfdsDir = L"");
    void shutdown();
    bool isEnabled() const;
    void setEnabled(bool enabled) { m_enabled = enabled; }
    bool showAllWindows() const;
    void setShowAllWindows(bool show);
    bool showSettingsTab() const;
    void setShowSettingsTab(bool show);
    int activeDisplayCount() const;
    void setActiveDisplayCount(int count);
    bool isHeadLocked() const;
    void setHeadLocked(bool locked);
    bool loadSettings();
    bool saveSettings();

    // Package & Screen Management (Tickets A6, A7)
    void setMfdsDirectory(const std::wstring& dir);
    const std::wstring& mfdsDirectory() const { return m_mfdsDir; }
    void discoverAndLoadPackages();
    static void applyAutoLayout(std::vector<MfdScreen>& screens);

    // Screen accessors
    const std::vector<MfdScreen>& screens() const { return m_screens; }
    std::vector<MfdScreen>& screens() { return m_screens; }
    size_t screenCount() const { return m_screens.size(); }
    MfdScreen* findScreen(const std::string& qualifiedOrScreenId);
    MfdScreen* findScreen(const std::string& packageId, const std::string& screenId);
    MfdScreen* focusedScreen();

    // Backwards compatibility slot API
    bool addSlot(std::string name, std::unique_ptr<IMfdProvider> provider, const MfdPose& pose);
    MfdSlot* findSlot(const std::string& name);
    const std::vector<MfdSlot>& slots() const { return m_screens; }
    std::vector<MfdSlot>& slots() { return m_screens; }
    size_t slotCount() const { return m_screens.size(); }
    MfdSlot* focusedSlot() { return focusedScreen(); }

    // Content watcher accessor
    MfdContentWatcher* contentWatcher() { return m_contentWatcher.get(); }

    // Hard cap queries (§10.2)
    size_t visibleScreenCount() const { return m_visibleScreenCount; }
    size_t suppressedScreenCount() const { return m_suppressedScreenCount; }

    // Telemetry (Ticket A9, §10.3)
    struct PerfTelemetry {
        double lastFrameCpuTimeMs = 0.0;
        double p50CpuTimeMs = 0.0;
        double p95CpuTimeMs = 0.0;
        double maxCpuTimeMs = 0.0;
        size_t visibleCount = 0;
        size_t dirtyCount = 0;
    };
    const PerfTelemetry& perfTelemetry() const { return m_perfTelemetry; }

    // INI paths & migration (§9.5)
    void setIniPathOverride(const std::wstring& path) { m_iniPathOverride = path; }
    std::wstring getIniPath() const;
    static bool migrateLegacyIni(const std::wstring& iniPath);

    // Per-frame update (called from VR render loop)
    void update(const Vec3& headPos, const Vec3& headForward, float dtSeconds);

    // Render all visible MFD screens
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
    bool m_enabled = true;
    bool m_showAllWindows = true;
    std::atomic<bool> m_showSettingsTab{true};
    int m_activeDisplayCount = 2;
    bool m_headLocked = false;
    bool m_packagesLoaded = false;
    int m_renderWidth = 512;
    int m_renderHeight = 384;
    DebugStats m_debugStats;

    Vec3 m_lastHeadPos{0.0f, 0.0f, 0.0f};
    Quat m_lastHeadRot{0.0f, 0.0f, 0.0f, 1.0f};

    std::wstring m_mfdsDir;
    std::wstring m_iniPathOverride;

    mutable std::mutex m_screensMutex;
    std::vector<MfdScreen> m_screens;
    std::unique_ptr<MfdContentWatcher> m_contentWatcher;
    MfdInputRouter m_inputRouter;
    std::unique_ptr<IMfdCompositor> m_compositor;

    // Hard cap tracking
    size_t m_visibleScreenCount = 0;
    size_t m_suppressedScreenCount = 0;
    bool m_capLogged = false;

    // QPC telemetry
    PerfTelemetry m_perfTelemetry;
    std::vector<double> m_cpuSamples;
    uint64_t m_lastPerfLogMs = 0;
    LARGE_INTEGER m_qpcFreq{};

    // Game context tracking for event emissions
    GameContextMask m_lastActiveContexts = 0;

    // D3D11 rendering resources
    Microsoft::WRL::ComPtr<ID3D11BlendState> m_blendState;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> m_quadVs;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> m_quadPs;
    Microsoft::WRL::ComPtr<ID3D11Buffer> m_quadCb;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> m_quadSampler;

    void setupScreenCallbacks(MfdScreen& screen);
    void ensureSettingsTab(MfdScreen& screen);
    void handleSettingsAdjustment(MfdScreen& screen, const std::string& key, int delta);
};

} // namespace edvr::mfd
