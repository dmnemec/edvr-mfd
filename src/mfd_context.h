#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <mutex>
#include <thread>
#include <atomic>
#include <functional>

namespace edvr::mfd {

// Individual game context bits (§7.2)
enum class GameContextBit : uint32_t {
    MainMenu         = 0,
    ShipCockpit      = 1,
    Fighter          = 2,
    Srv              = 3,
    OnFoot           = 4,
    Taxi             = 5,
    Multicrew        = 6,
    Docked           = 7,
    Landed           = 8,
    Supercruise      = 9,
    NormalSpace      = 10,
    Hardpoints       = 11,
    AnalysisMode     = 12,
    CombatMode       = 13,
    GalaxyMap        = 14,
    SystemMap        = 15,
    Orrery           = 16,
    Fss              = 17,
    Saa              = 18,
    Codex            = 19,
    StationServices  = 20,
    Paused           = 21, // Reserved (§7.2, §7.3)
    Count            = 22
};

using GameContextMask = uint64_t;

namespace GameContext {
    constexpr GameContextMask None            = 0;
    constexpr GameContextMask MainMenu        = 1ULL << static_cast<uint32_t>(GameContextBit::MainMenu);
    constexpr GameContextMask ShipCockpit     = 1ULL << static_cast<uint32_t>(GameContextBit::ShipCockpit);
    constexpr GameContextMask Fighter         = 1ULL << static_cast<uint32_t>(GameContextBit::Fighter);
    constexpr GameContextMask Srv             = 1ULL << static_cast<uint32_t>(GameContextBit::Srv);
    constexpr GameContextMask OnFoot          = 1ULL << static_cast<uint32_t>(GameContextBit::OnFoot);
    constexpr GameContextMask Taxi            = 1ULL << static_cast<uint32_t>(GameContextBit::Taxi);
    constexpr GameContextMask Multicrew       = 1ULL << static_cast<uint32_t>(GameContextBit::Multicrew);
    constexpr GameContextMask Docked          = 1ULL << static_cast<uint32_t>(GameContextBit::Docked);
    constexpr GameContextMask Landed          = 1ULL << static_cast<uint32_t>(GameContextBit::Landed);
    constexpr GameContextMask Supercruise     = 1ULL << static_cast<uint32_t>(GameContextBit::Supercruise);
    constexpr GameContextMask NormalSpace     = 1ULL << static_cast<uint32_t>(GameContextBit::NormalSpace);
    constexpr GameContextMask Hardpoints      = 1ULL << static_cast<uint32_t>(GameContextBit::Hardpoints);
    constexpr GameContextMask AnalysisMode    = 1ULL << static_cast<uint32_t>(GameContextBit::AnalysisMode);
    constexpr GameContextMask CombatMode      = 1ULL << static_cast<uint32_t>(GameContextBit::CombatMode);
    constexpr GameContextMask GalaxyMap       = 1ULL << static_cast<uint32_t>(GameContextBit::GalaxyMap);
    constexpr GameContextMask SystemMap       = 1ULL << static_cast<uint32_t>(GameContextBit::SystemMap);
    constexpr GameContextMask Orrery          = 1ULL << static_cast<uint32_t>(GameContextBit::Orrery);
    constexpr GameContextMask Fss             = 1ULL << static_cast<uint32_t>(GameContextBit::Fss);
    constexpr GameContextMask Saa             = 1ULL << static_cast<uint32_t>(GameContextBit::Saa);
    constexpr GameContextMask Codex           = 1ULL << static_cast<uint32_t>(GameContextBit::Codex);
    constexpr GameContextMask StationServices = 1ULL << static_cast<uint32_t>(GameContextBit::StationServices);
    constexpr GameContextMask Paused          = 1ULL << static_cast<uint32_t>(GameContextBit::Paused);

    // Global exclusion set (§7.3)
    // No member may be active unless the screen lists it explicitly in contexts.
    constexpr GameContextMask GlobalExclusionSet =
        MainMenu | GalaxyMap | SystemMap | Orrery | Fss | Saa | Codex | Paused;

    // Default screen contexts (§7.3)
    constexpr GameContextMask DefaultScreenContexts =
        ShipCockpit | Fighter | Srv | OnFoot | Taxi | Multicrew;
}

// Elite Dangerous Status.json Flags bit constants (Bit 0 to Bit 31)
constexpr uint32_t kFlagDocked               = 1u << 0;  // Docked, on a landing pad
constexpr uint32_t kFlagLanded               = 1u << 1;  // Landed, on planet surface
constexpr uint32_t kFlagLandingGearDown      = 1u << 2;  // Landing Gear Down
constexpr uint32_t kFlagShieldsUp            = 1u << 3;  // Shields Up
constexpr uint32_t kFlagSupercruise          = 1u << 4;  // Supercruise
constexpr uint32_t kFlagFlightAssistOff      = 1u << 5;  // Flight Assist Off
constexpr uint32_t kFlagHardpointsDeployed   = 1u << 6;  // Hardpoints Deployed
constexpr uint32_t kFlagInWing               = 1u << 7;  // In Wing / Team
constexpr uint32_t kFlagLightsOn             = 1u << 8;  // Ship Lights On
constexpr uint32_t kFlagCargoScoopDeployed   = 1u << 9;  // Cargo Scoop Deployed
constexpr uint32_t kFlagSilentRunning        = 1u << 10; // Silent Running
constexpr uint32_t kFlagScoopingFuel         = 1u << 11; // Scooping Fuel
constexpr uint32_t kFlagSrvHandbrake         = 1u << 12; // SRV Handbrake
constexpr uint32_t kFlagSrvUsingTurret       = 1u << 13; // SRV using Turret view
constexpr uint32_t kFlagSrvTurretRetracted   = 1u << 14; // SRV Turret retracted
constexpr uint32_t kFlagSrvDriveAssist       = 1u << 15; // SRV Drive Assist
constexpr uint32_t kFlagFsdMassLocked        = 1u << 16; // FSD MassLocked
constexpr uint32_t kFlagFsdCharging          = 1u << 17; // FSD Charging
constexpr uint32_t kFlagFsdCooldown          = 1u << 18; // FSD Cooldown
constexpr uint32_t kFlagLowFuel              = 1u << 19; // Low Fuel (< 25%)
constexpr uint32_t kFlagOverheating          = 1u << 20; // Over Heating (> 100%)
constexpr uint32_t kFlagHasLatLong           = 1u << 21; // Has Lat/Long coordinates
constexpr uint32_t kFlagIsInDanger           = 1u << 22; // Is In Danger
constexpr uint32_t kFlagBeingInterdicted     = 1u << 23; // Being Interdicted
constexpr uint32_t kFlagInMainShip           = 1u << 24; // In Main Ship
constexpr uint32_t kFlagInFighter            = 1u << 25; // In Fighter
constexpr uint32_t kFlagInSRV                = 1u << 26; // In SRV
constexpr uint32_t kFlagHudInAnalysisMode    = 1u << 27; // HUD in Analysis mode
constexpr uint32_t kFlagNightVision          = 1u << 28; // Night Vision
constexpr uint32_t kFlagAltFromAvgRadius     = 1u << 29; // Altitude from Average radius
constexpr uint32_t kFlagFsdJump              = 1u << 30; // FSD Jump (hyperspace)
constexpr uint32_t kFlagSrvHighBeam          = 1u << 31; // SRV High Beam

// Elite Dangerous Status.json Flags2 bit constants (Odyssey on-foot / multicrew)
constexpr uint32_t kFlag2OnFoot              = 1u << 0;  // On Foot
constexpr uint32_t kFlag2InTaxi              = 1u << 1;  // In Taxi
constexpr uint32_t kFlag2InMulticrew         = 1u << 2;  // In Multicrew (crew member)
constexpr uint32_t kFlag2OnFootInStation     = 1u << 3;  // On Foot in Station
constexpr uint32_t kFlag2OnFootOnPlanet      = 1u << 4;  // On Foot on Planet
constexpr uint32_t kFlag2AimDownSight        = 1u << 5;  // Aim Down Sight
constexpr uint32_t kFlag2LowOxygen           = 1u << 6;  // Low Oxygen
constexpr uint32_t kFlag2LowHealth           = 1u << 7;  // Low Health
constexpr uint32_t kFlag2Cold                = 1u << 8;  // Cold
constexpr uint32_t kFlag2Hot                 = 1u << 9;  // Hot
constexpr uint32_t kFlag2VeryCold            = 1u << 10; // Very Cold
constexpr uint32_t kFlag2VeryHot             = 1u << 11; // Very Hot
constexpr uint32_t kFlag2GlideMode           = 1u << 12; // Glide Mode
constexpr uint32_t kFlag2OnFootInHangar      = 1u << 13; // On Foot in Hangar
constexpr uint32_t kFlag2OnFootSocialSpace   = 1u << 14; // On Foot Social Space
constexpr uint32_t kFlag2OnFootExterior      = 1u << 15; // On Foot Exterior
constexpr uint32_t kFlag2BreathableAtmosphere= 1u << 16; // Breathable Atmosphere
constexpr uint32_t kFlag2TelepresenceMulticrew = 1u << 17; // Telepresence Multicrew
constexpr uint32_t kFlag2PhysicalMulticrew   = 1u << 18; // Physical Multicrew
constexpr uint32_t kFlag2FsdHyperdriveCharging = 1u << 19; // FSD Hyperdrive Charging

// Elite Dangerous Status.json GuiFocus constants
constexpr int kGuiFocusNoFocus         = 0;
constexpr int kGuiFocusInternalPanel   = 1;  // Right hand side panel (modules, status)
constexpr int kGuiFocusExternalPanel   = 2;  // Left hand side panel (navigation, transactions)
constexpr int kGuiFocusCommsPanel      = 3;  // Top panel (comms)
constexpr int kGuiFocusRolePanel       = 4;  // Bottom panel (helm, fighter, SRV)
constexpr int kGuiFocusStationServices = 5;
constexpr int kGuiFocusGalaxyMap       = 6;
constexpr int kGuiFocusSystemMap       = 7;
constexpr int kGuiFocusOrrery          = 8;
constexpr int kGuiFocusFSS             = 9;
constexpr int kGuiFocusSAA             = 10; // Detailed surface scanner
constexpr int kGuiFocusCodex           = 11;

// Snapshot of game status read from Status.json
struct GameStatusSnapshot {
    uint32_t flags = 0;
    uint32_t flags2 = 0;
    int guiFocus = 0;
    bool valid = false;
};

// Thread-safe evaluator maintaining active game contexts from Status.json (§7)
class ContextEvaluator {
public:
    static ContextEvaluator& instance();

    ContextEvaluator();
    ~ContextEvaluator();

    ContextEvaluator(const ContextEvaluator&) = delete;
    ContextEvaluator& operator=(const ContextEvaluator&) = delete;

    // Background watcher thread lifecycle (reads Status.json off the render thread)
    void startWatcher(uint32_t pollIntervalMs = 100);
    void stopWatcher();
    bool isWatcherRunning() const;

    // Manual or single poll update (returns true if active contexts changed)
    bool pollOnce();
    bool updateFromStatus(uint32_t flags, uint32_t flags2, int guiFocus);

    // Queries (thread-safe)
    GameContextMask getActiveContexts() const;
    std::vector<std::string> getActiveContextNames() const;
    GameStatusSnapshot getSnapshot() const;
    bool isContextActive(GameContextMask mask) const;

    // Screen visibility matching against current active contexts (§7.3)
    bool isVisible(GameContextMask screenContexts, GameContextMask excludeContexts = 0) const;
    bool isVisible(const std::vector<std::string>& contexts, const std::vector<std::string>& excludeContexts = {}) const;

    // Optional notification callback when context changes
    using ContextChangeCallback = std::function<void(GameContextMask oldCtx, GameContextMask newCtx, const GameStatusSnapshot& snapshot)>;
    void setContextChangeCallback(ContextChangeCallback cb);

    // Pure evaluation functions (§7.2, §7.3)
    static GameContextMask evaluate(uint32_t flags, uint32_t flags2, int guiFocus);
    static bool isScreenVisible(GameContextMask screenContexts,
                                GameContextMask excludeContexts,
                                GameContextMask activeContexts);
    static bool isScreenVisible(const std::vector<std::string>& contexts,
                                const std::vector<std::string>& excludeContexts,
                                GameContextMask activeContexts);

    // Vocabulary converters
    static const char* contextToString(GameContextBit bit);
    static GameContextMask contextFromString(const std::string& name);
    static GameContextMask parseContexts(const std::vector<std::string>& names);
    static std::vector<std::string> formatContexts(GameContextMask mask);

    // Directory resolution (Ticket A13)
    // Priority: programmatic override -> EDVR_MFD_JOURNAL_DIR env var -> %USERPROFILE%\Saved Games\...
    static std::wstring getJournalDir();
    static void setJournalDirOverride(const std::wstring& path);
    static std::wstring getStatusJsonPath();

    // Low-level Win32 file reading & JSON parsing (no CRT stdio)
    static bool readAndParseStatus(const std::wstring& filePath,
                                   uint32_t& outFlags,
                                   uint32_t& outFlags2,
                                   int& outGuiFocus);
    static bool parseStatusJson(const std::string& jsonText,
                                uint32_t& outFlags,
                                uint32_t& outFlags2,
                                int& outGuiFocus);

private:
    void workerLoop(uint32_t pollIntervalMs);

    mutable std::mutex m_mutex;
    GameStatusSnapshot m_snapshot;
    GameContextMask m_activeContexts = GameContext::MainMenu;
    std::vector<std::string> m_activeContextNames;
    uint64_t m_lastFileTime = 0;
    ContextChangeCallback m_changeCallback;

    std::atomic<bool> m_running{false};
    std::thread m_workerThread;

    static std::wstring s_journalDirOverride;
    static std::mutex s_overrideMutex;
};

} // namespace edvr::mfd
