# EDVR MFD

Floating cockpit displays rendered in VR inside Elite Dangerous, filled with content by external companion processes.

## Language

### Screens and layout

**Layout template**:
The arrangement a screen uses to present its content, declared per screen in the package manifest: `standard` or `compact_banner`.
_Avoid_: layout mode, skin, style

**Banner**:
A screen using the `compact_banner` layout template: a single horizontal strip of blocks with no header, tab bar or footer.
_Avoid_: compact MFD, strip, bar

**View**:
One tab of a banner, shown one at a time and filling the whole strip.
_Avoid_: page (except for the fixed SETTINGS pages), banner tab

**Block**:
One cell of a banner view, either pressable or display-only, sized by a relative width weight.
_Avoid_: button (for display-only cells), cell, widget

**Status badge**:
A short severity-coloured label reporting a screen's health or state (e.g. `LIVE`, `STALE`, `ERR`), set by the package or overridden by the plugin when something fails.
_Avoid_: status tag, indicator
