#pragma once

#include "mfd_types.h"
#include <string>
#include <vector>
#include <map>
#include <memory>

namespace edvr::mfd {

// 3D pose specification in package manifest (§4.2)
struct MfdManifestPose {
    bool hasPose = false;
    float position[3] = {0.0f, 0.0f, 0.0f}; // [x, y, z] in cockpit meters
    float pitchDeg = 0.0f;
    float yawDeg = 0.0f;
    float rollDeg = 0.0f;
    float widthM = 0.24f;
    float heightM = 0.16f;

    MfdPose toMfdPose() const {
        MfdPose p;
        p.position = Vec3(position[0], position[1], position[2]);
        p.orientation = Quat::fromEulerDegrees(pitchDeg, yawDeg, rollDeg);
        p.widthM = widthM;
        p.heightM = heightM;
        return p;
    }
};

// Screen entry declared in manifest.json (§4.2)
struct MfdScreenManifest {
    std::string id;                          // Required unique screen id within package
    std::string file;                        // Required relative path inside package folder
    std::string title;                       // Screen title displayed in menus
    MfdManifestPose pose;                    // Optional placement (auto-layout if omitted)
    std::vector<std::string> contexts;       // Contexts where screen is visible (defaults to §7.3)
    std::vector<std::string> excludeContexts; // Explicit context exclusions
    bool autoHideUntilGaze = false;          // Gaze fade toggle
};

// Supervisor restart policy for companion executables (§4.2, §8.2)
enum class MfdCompanionRestart : uint8_t {
    kOnFailure = 0,
    kAlways,
    kNever
};

// Companion background executable definition (§4.2, §8.1)
struct MfdCompanionManifest {
    std::string id;                          // Companion identifier
    std::string exe;                         // Relative inside package or absolute path
    std::vector<std::string> args;           // CLI arguments
    std::map<std::string, std::string> env;  // Additional environment variables
    std::string restart = "on-failure";      // "on-failure" | "always" | "never"
    MfdCompanionRestart restartPolicy = MfdCompanionRestart::kOnFailure;
};

// Parsed package manifest model (§4.2)
struct MfdPackageManifest {
    int manifestVersion = 1;
    std::string id;                          // Unique package ID matching directory name
    std::string name;                        // Human-readable package name
    std::string version;                     // Package version string
    std::string author;                      // Author name / attribution
    std::string description;                 // Description of package
    std::string minPluginVersion;            // Minimum required plugin version
    std::vector<MfdScreenManifest> screens;  // Screens defined by this package
    std::vector<MfdCompanionManifest> companions; // Companions managed for this package

    // Runtime resolved metadata:
    std::wstring packageDir;                 // Absolute path to package directory on disk
};

// Package ID validation per regex ^[a-z0-9][a-z0-9_-]{1,31}$
bool isValidPackageId(const std::string& id);

// Security checks against path traversal attacks:
// Relative paths must not escape package root (e.g. "../" or "a/../../b")
bool isPathSafeRelative(const std::string& pathStr);

// Exe path must resolve inside package root or be an absolute path (§4.2)
bool isExePathSafe(const std::string& exeStr);

// Default screen contexts (§7.3: [ship_cockpit, fighter, srv, on_foot, taxi, multicrew])
const std::vector<std::string>& getDefaultScreenContexts();

// Parses manifest.json content from string.
// If expectedDirName is provided and non-empty, manifest ID must match it.
bool parseManifestJson(const std::string& jsonText,
                       MfdPackageManifest& outManifest,
                       const std::string& expectedDirName = "",
                       std::string* outError = nullptr);

// Loads and parses manifest.json from a file on disk.
bool loadPackageManifest(const std::wstring& manifestPath,
                         MfdPackageManifest& outManifest,
                         const std::string& expectedDirName = "",
                         std::string* outError = nullptr);

// Discovers packages by scanning an mfds/ directory.
// Layout: <mfdsDir>/<package_id>/manifest.json.
// Sorts packages by package id ascending, preserving screen declaration order.
std::vector<MfdPackageManifest> discoverPackages(const std::wstring& mfdsDir);

// Gets the default mfds/ directory relative to plugin installation
std::wstring getDefaultMfdsDirectory();

} // namespace edvr::mfd
