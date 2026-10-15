package main

import (
	"fmt"
	"regexp"
	"strings"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
)

// Slot groups matching §12.1 ordering: core -> optional -> hardpoints -> utility.
const (
	SlotGroupCore       = 1
	SlotGroupOptional   = 2
	SlotGroupHardpoint  = 3
	SlotGroupUtility    = 4
)

// Standard Elite Dangerous power plant capacities in MW.
var standardPowerPlantCapacity = map[string]float64{
	// Size 1
	"1e": 4.80, "1d": 5.40, "1c": 6.00, "1b": 6.48, "1a": 7.20,
	// Size 2
	"2e": 6.40, "2d": 7.20, "2c": 8.00, "2b": 8.64, "2a": 9.60,
	// Size 3
	"3e": 8.00, "3d": 9.00, "3c": 10.00, "3b": 10.80, "3a": 12.00,
	// Size 4
	"4e": 10.40, "4d": 11.70, "4c": 13.00, "4b": 14.04, "4a": 15.60,
	// Size 5
	"5e": 13.60, "5d": 15.30, "5c": 17.00, "5b": 18.36, "5a": 20.40,
	// Size 6
	"6e": 16.80, "6d": 18.90, "6c": 21.00, "6b": 22.68, "6a": 25.20,
	// Size 7
	"7e": 20.00, "7d": 22.50, "7c": 25.00, "7b": 27.00, "7a": 30.00,
	// Size 8
	"8e": 24.00, "8d": 27.00, "8c": 30.00, "8b": 32.40, "8a": 36.00,
}

var guardianPowerPlantCapacity = map[int]float64{
	2: 10.56,
	3: 13.20,
	4: 17.16,
	5: 22.44,
	6: 27.72,
	7: 33.00,
	8: 39.60,
}

var (
	sizeClassRegex = regexp.MustCompile(`(?i)size([1-8])_class([1-5])`)
	guardianRegex  = regexp.MustCompile(`(?i)guardian.*size([1-8])|size([1-8]).*guardian`)
)

// ClassNumberToLetter maps Elite internal class 1..5 to rating E..A.
func ClassNumberToLetter(classNum string) string {
	switch classNum {
	case "1":
		return "E"
	case "2":
		return "D"
	case "3":
		return "C"
	case "4":
		return "B"
	case "5":
		return "A"
	default:
		return classNum
	}
}

// ExtractClassRating extracts rating like "5A", "2D" from internal item symbol.
func ExtractClassRating(item string) string {
	m := sizeClassRegex.FindStringSubmatch(item)
	if len(m) == 3 {
		return m[1] + ClassNumberToLetter(m[2])
	}
	// Check for Guardian hybrid
	if strings.Contains(strings.ToLower(item), "guardian") {
		for s := 1; s <= 8; s++ {
			if strings.Contains(item, fmt.Sprintf("size%d", s)) || strings.Contains(item, fmt.Sprintf("_%d_", s)) {
				return fmt.Sprintf("%dA", s)
			}
		}
	}
	return ""
}

// GetPowerPlantCapacity returns the MW capacity of the power plant module.
// Capacity from bundled power-plant table overridden by Engineering PowerCapacity modifier if present (§12.1).
func GetPowerPlantCapacity(item string, eng *elite.ModuleEngineering) float64 {
	// 1. Engineering modifier override
	if eng != nil {
		for _, mod := range eng.Modifiers {
			if strings.EqualFold(mod.Label, "PowerCapacity") && mod.Value > 0 {
				return mod.Value
			}
		}
	}

	lower := strings.ToLower(item)

	// 2. Guardian hybrid
	if strings.Contains(lower, "guardian") {
		for sz, capMW := range guardianPowerPlantCapacity {
			if strings.Contains(lower, fmt.Sprintf("size%d", sz)) {
				return capMW
			}
		}
	}

	// 3. Standard power plants
	cr := strings.ToLower(ExtractClassRating(item))
	if capMW, ok := standardPowerPlantCapacity[cr]; ok {
		return capMW
	}

	// Fallback reasonable default if unlisted or unrecognized
	return 15.0
}

// SlotGroup returns an integer priority for module slot sorting (§12.1):
// Core internals (1) -> Optional internals (2) -> Hardpoints (3) -> Utility (4).
func SlotGroup(slot string) int {
	s := strings.ToLower(slot)
	switch {
	case strings.HasPrefix(s, "tinyhardpoint"):
		return SlotGroupUtility
	case strings.Contains(s, "hardpoint"):
		return SlotGroupHardpoint
	case s == "armour" || s == "powerplant" || s == "mainengines" ||
		s == "frameshiftdrive" || s == "lifesupport" || s == "powerdistributor" ||
		s == "radar" || s == "fueltank" || strings.HasPrefix(s, "core"):
		return SlotGroupCore
	default:
		// Optional internals: Slot01_Size..., Military01, PlanetaryApproachSuite, CargoHatch, etc.
		return SlotGroupOptional
	}
}

// FormatSlotName humanizes slot names like "FrameShiftDrive" -> "Frame Shift Drive".
func FormatSlotName(slot string) string {
	switch strings.ToLower(slot) {
	case "armour":
		return "Armour"
	case "powerplant":
		return "Power Plant"
	case "mainengines":
		return "Thrusters"
	case "frameshiftdrive":
		return "Frame Shift Drive"
	case "lifesupport":
		return "Life Support"
	case "powerdistributor":
		return "Power Distributor"
	case "radar":
		return "Sensors"
	case "fueltank":
		return "Fuel Tank"
	case "planetaryapproachsuite":
		return "Planetary Suite"
	case "cargohatch":
		return "Cargo Hatch"
	}

	// Clean up internal slot names like "Slot01_Size5" -> "Slot 01 (Size 5)"
	if strings.HasPrefix(strings.ToLower(slot), "slot") {
		parts := strings.Split(slot, "_")
		if len(parts) == 2 {
			sizePart := strings.Replace(parts[1], "Size", "Size ", 1)
			return strings.Replace(parts[0], "Slot", "Slot ", 1) + " (" + sizePart + ")"
		}
	}
	return slot
}

// FormatModuleName returns a localized / clean readable name for an Elite item.
func FormatModuleName(item string) string {
	lower := strings.ToLower(item)
	clean := elite.CleanItemName(item)

	// Hardpoints
	switch {
	case strings.Contains(lower, "multicannon"):
		return "Multi-cannon"
	case strings.Contains(lower, "beamlaser"):
		return "Beam Laser"
	case strings.Contains(lower, "pulselaser"):
		return "Pulse Laser"
	case strings.Contains(lower, "burstlaser"):
		return "Burst Laser"
	case strings.Contains(lower, "plasmaaccelerator"):
		return "Plasma Accelerator"
	case strings.Contains(lower, "railgun"):
		return "Railgun"
	case strings.Contains(lower, "cannon"):
		return "Cannon"
	case strings.Contains(lower, "missilerack"):
		return "Missile Rack"
	case strings.Contains(lower, "torpedopylon"):
		return "Torpedo Pylon"
	case strings.Contains(lower, "minelauncher"):
		return "Mine Launcher"
	case strings.Contains(lower, "fragmentcannon"):
		return "Fragment Cannon"
	case strings.Contains(lower, "pointdefence"):
		return "Point Defence"
	case strings.Contains(lower, "chafflauncher"):
		return "Chaff Launcher"
	case strings.Contains(lower, "heatsinklauncher"):
		return "Heat Sink Launcher"
	case strings.Contains(lower, "electroniccountermeasure"):
		return "Electronic Countermeasure"
	}

	// Core & Optional Modules
	switch {
	case strings.Contains(lower, "powerplant"):
		if strings.Contains(lower, "guardian") {
			return "Guardian Hybrid Power Plant"
		}
		return "Power Plant"
	case strings.Contains(lower, "hyperdrive") || strings.Contains(lower, "frameshiftdrive"):
		return "Frame Shift Drive"
	case strings.Contains(lower, "engine"):
		return "Thrusters"
	case strings.Contains(lower, "powerdistributor"):
		return "Power Distributor"
	case strings.Contains(lower, "lifesupport"):
		return "Life Support"
	case strings.Contains(lower, "sensors") || strings.Contains(lower, "radar"):
		return "Sensors"
	case strings.Contains(lower, "fueltank"):
		return "Fuel Tank"
	case strings.Contains(lower, "shieldgenerator"):
		return "Shield Generator"
	case strings.Contains(lower, "shieldbooster"):
		return "Shield Booster"
	case strings.Contains(lower, "cargorack"):
		return "Cargo Rack"
	case strings.Contains(lower, "hullreinforcement"):
		return "Hull Reinforcement Package"
	case strings.Contains(lower, "modulereinforcement"):
		return "Module Reinforcement Package"
	case strings.Contains(lower, "repairer") || strings.Contains(lower, "afmu"):
		return "Auto Field-Maintenance Unit"
	case strings.Contains(lower, "fuelscoop"):
		return "Fuel Scoop"
	case strings.Contains(lower, "detailedsurfacescanner"):
		return "Detailed Surface Scanner"
	case strings.Contains(lower, "fsdinterdictor"):
		return "FSD Interdictor"
	case strings.Contains(lower, "collectorlimpet") || strings.Contains(lower, "dronecontrol_collector"):
		return "Collector Limpet Controller"
	case strings.Contains(lower, "prospectorlimpet") || strings.Contains(lower, "dronecontrol_prospector"):
		return "Prospector Limpet Controller"
	case strings.Contains(lower, "hatchbreakerlimpet"):
		return "Hatch Breaker Limpet"
	case strings.Contains(lower, "planetaryapproachsuite"):
		return "Planetary Approach Suite"
	case strings.Contains(lower, "armour"):
		return "Composite Armour"
	}

	if clean != "" {
		// Capitalize words in clean string
		words := strings.Fields(strings.ReplaceAll(clean, "_", " "))
		for i, w := range words {
			if len(w) > 0 {
				words[i] = strings.ToUpper(w[:1]) + strings.ToLower(w[1:])
			}
		}
		return strings.Join(words, " ")
	}

	return item
}

// FormatModuleSublabel returns sublabel = class/rating + slot per §12.1.
// Example: "5A • Frame Shift Drive" or "HugeHardpoint1 • 4A".
func FormatModuleSublabel(slot, item string) string {
	cr := ExtractClassRating(item)
	fSlot := FormatSlotName(slot)
	if cr != "" {
		return fmt.Sprintf("%s • %s", cr, fSlot)
	}
	return fSlot
}
