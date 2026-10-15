#pragma once

#include "mfd_types.h"
#include "mfd_view_model.h"
#include <string>

namespace edvr::mfd {

// Parse error report providing line, column, and descriptive message (§5.2, §5.3)
struct MfdParseError {
    int line = 0;
    int column = 0;
    std::string message;
};

// Parses a display JSON string according to Schema v1 (§5.3).
// Populates outVm on success.
// If parsing or validation fails, returns false and populates outError (if non-null).
// An optional palette can be supplied to resolve named colors; if null, default amber is used.
bool parseDisplayJson(const std::string& jsonText,
                      MfdViewModel& outVm,
                      MfdParseError* outError = nullptr,
                      const MfdPaletteSet* palette = nullptr);

bool parseDisplayJson(const char* data,
                      size_t length,
                      MfdViewModel& outVm,
                      MfdParseError* outError = nullptr,
                      const MfdPaletteSet* palette = nullptr);

// Named color helpers (§5.3: normal, dim, accent, ok, warn, crit)
MfdNamedColor parseNamedColor(const std::string& name);
const char* namedColorToString(MfdNamedColor color);
MfdColor resolveNamedColor(MfdNamedColor color, const MfdPaletteSet& theme);
MfdColor resolveNamedColor(const std::string& name, const MfdPaletteSet& theme);

// Icon helpers (§5.3: fuel, warning, neutron, whitedwarf, bio, geo, check, lock, star)
MfdIcon parseIcon(const std::string& name);
const char* iconToString(MfdIcon icon);

// Check state helpers (§5.3: done, todo, na)
MfdCheckState parseCheckState(const std::string& state);
const char* checkStateToString(MfdCheckState state);

// Action helpers (§5.3: 'event' | 'clipboard')
MfdItemAction parseItemAction(const std::string& action);
const char* itemActionToString(MfdItemAction action);

} // namespace edvr::mfd
