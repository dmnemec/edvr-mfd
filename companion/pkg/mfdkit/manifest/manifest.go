package manifest

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"regexp"
	"strings"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
)

// ValidContexts defines the allowable context strings per §4.2 & §7.3.
var ValidContexts = map[string]bool{
	"main_menu":        true,
	"ship_cockpit":     true,
	"fighter":          true,
	"srv":              true,
	"on_foot":          true,
	"taxi":             true,
	"multicrew":        true,
	"docked":           true,
	"landed":           true,
	"supercruise":      true,
	"normal_space":     true,
	"hardpoints":       true,
	"analysis_mode":    true,
	"combat_mode":      true,
	"galaxy_map":       true,
	"system_map":       true,
	"station_services": true,
}

// ValidRestartPolicies defines valid companion restart values (§4.2).
var ValidRestartPolicies = map[string]bool{
	"on-failure": true,
	"always":     true,
	"never":      true,
}

var packageIDRegex = regexp.MustCompile(`^[a-z0-9][a-z0-9_-]{1,31}$`)

// IsValidPackageID checks if id matches ^[a-z0-9][a-z0-9_-]{1,31}$ (§4.1).
func IsValidPackageID(id string) bool {
	return packageIDRegex.MatchString(id)
}

// IsPathSafeRelative verifies that a relative path does not escape the package root.
func IsPathSafeRelative(p string) bool {
	if p == "" || strings.ContainsRune(p, 0) {
		return false
	}
	// Absolute path indicators (Windows drive letter or leading slashes)
	if len(p) >= 2 && ((p[0] >= 'a' && p[0] <= 'z') || (p[0] >= 'A' && p[0] <= 'Z')) && p[1] == ':' {
		return false
	}
	if p[0] == '/' || p[0] == '\\' {
		return false
	}

	depth := 0
	cleaned := filepath.ToSlash(p)
	tokens := strings.Split(cleaned, "/")
	for _, tok := range tokens {
		if tok == "" || tok == "." {
			continue
		} else if tok == ".." {
			if depth <= 0 {
				return false
			}
			depth--
		} else {
			depth++
		}
	}
	return true
}

// IsExePathSafe checks whether an executable path is safe (§4.2):
// Must resolve inside the package folder or be an absolute path.
func IsExePathSafe(exe string) bool {
	if exe == "" || strings.ContainsRune(exe, 0) {
		return false
	}
	if filepath.IsAbs(exe) {
		return true
	}
	// Windows drive letter check on non-Windows platforms
	if len(exe) >= 2 && ((exe[0] >= 'a' && exe[0] <= 'z') || (exe[0] >= 'A' && exe[0] <= 'Z')) && exe[1] == ':' {
		return true
	}
	return IsPathSafeRelative(exe)
}

// Pose represents default 3D placement in cockpit space (§4.2).
type Pose struct {
	Position []float64 `json:"position"`
	PitchDeg float64   `json:"pitchDeg"`
	YawDeg   float64   `json:"yawDeg"`
	RollDeg  float64   `json:"rollDeg"`
	WidthM   float64   `json:"widthM"`
	HeightM  float64   `json:"heightM"`
}

// Screen represents a screen declared in manifest.json (§4.2).
type Screen struct {
	ID                string   `json:"id"`
	File              string   `json:"file"`
	Title             string   `json:"title"`
	Pose              *Pose    `json:"pose,omitempty"`
	Contexts          []string `json:"contexts,omitempty"`
	ExcludeContexts   []string `json:"excludeContexts,omitempty"`
	AutoHideUntilGaze bool     `json:"autoHideUntilGaze,omitempty"`
}

// Companion represents a background companion executable declared in manifest.json (§4.2).
type Companion struct {
	ID      string            `json:"id"`
	Exe     string            `json:"exe"`
	Args    []string          `json:"args,omitempty"`
	Env     map[string]string `json:"env,omitempty"`
	Restart string            `json:"restart,omitempty"`
}

// Manifest represents a parsed package manifest (§4.2).
type Manifest struct {
	ManifestVersion  int         `json:"manifestVersion"`
	ID               string      `json:"id"`
	Name             string      `json:"name"`
	Version          string      `json:"version,omitempty"`
	Author           string      `json:"author,omitempty"`
	Description      string      `json:"description,omitempty"`
	MinPluginVersion string      `json:"minPluginVersion,omitempty"`
	Screens          []Screen    `json:"screens"`
	Companions       []Companion `json:"companions,omitempty"`
}

// ValidatePackage loads and validates manifest.json and referenced screens in packageDir.
func ValidatePackage(packageDir string) (*Manifest, []string) {
	var errs []string

	fi, err := os.Stat(packageDir)
	if err != nil {
		return nil, []string{fmt.Sprintf("package directory %q inaccessible: %v", packageDir, err)}
	}
	if !fi.IsDir() {
		return nil, []string{fmt.Sprintf("path %q is not a directory", packageDir)}
	}

	manifestPath := filepath.Join(packageDir, "manifest.json")
	data, err := os.ReadFile(manifestPath)
	if err != nil {
		return nil, []string{fmt.Sprintf("failed to read manifest.json: %v", err)}
	}

	absDir, _ := filepath.Abs(packageDir)
	dirBase := filepath.Base(absDir)

	m, manifestErrs := ValidateManifestData(data, dirBase)
	errs = append(errs, manifestErrs...)

	if m != nil {
		screenErrs := ValidateScreens(packageDir, m)
		errs = append(errs, screenErrs...)

		compErrs := ValidateCompanions(m)
		errs = append(errs, compErrs...)
	}

	return m, errs
}

// ValidateManifestData parses and validates raw manifest JSON.
func ValidateManifestData(data []byte, expectedDirName string) (*Manifest, []string) {
	var errs []string
	var m Manifest

	if err := json.Unmarshal(data, &m); err != nil {
		return nil, []string{fmt.Sprintf("manifest.json contains invalid JSON: %v", err)}
	}

	if m.ManifestVersion != 1 {
		errs = append(errs, fmt.Sprintf("manifestVersion must be 1, got %d", m.ManifestVersion))
	}

	if m.ID == "" {
		errs = append(errs, "manifest 'id' is required and cannot be empty")
	} else {
		if !IsValidPackageID(m.ID) {
			errs = append(errs, fmt.Sprintf("manifest 'id' %q does not match pattern ^[a-z0-9][a-z0-9_-]{1,31}$", m.ID))
		}
		if expectedDirName != "" && IsValidPackageID(expectedDirName) && expectedDirName != m.ID {
			errs = append(errs, fmt.Sprintf("manifest 'id' %q does not match package directory name %q", m.ID, expectedDirName))
		}
	}

	if strings.TrimSpace(m.Name) == "" {
		errs = append(errs, "manifest 'name' is required and cannot be empty")
	}

	if len(m.Screens) == 0 {
		errs = append(errs, "manifest 'screens' must contain at least one screen declaration")
	}
	if len(m.Screens) > display.MaxTabs {
		errs = append(errs, fmt.Sprintf("manifest declares %d screens, exceeding maximum limit of %d", len(m.Screens), display.MaxTabs))
	}

	return &m, errs
}

// ValidateScreens checks that all declared screens exist, have valid fields, and valid display JSON.
func ValidateScreens(packageDir string, m *Manifest) []string {
	var errs []string
	seenScreenIDs := make(map[string]bool, len(m.Screens))

	for idx, s := range m.Screens {
		prefix := fmt.Sprintf("screens[%d]", idx)
		if s.ID == "" {
			errs = append(errs, fmt.Sprintf("%s has empty id", prefix))
		} else {
			prefix = fmt.Sprintf("screen[%q]", s.ID)
			if seenScreenIDs[s.ID] {
				errs = append(errs, fmt.Sprintf("duplicate screen id %q", s.ID))
			}
			seenScreenIDs[s.ID] = true
		}

		if s.File == "" {
			errs = append(errs, fmt.Sprintf("%s has empty file path", prefix))
			continue
		}

		if !IsPathSafeRelative(s.File) {
			errs = append(errs, fmt.Sprintf("%s file path %q is unsafe or escapes package directory", prefix, s.File))
			continue
		}

		// Verify screen file exists and validates against Schema v1
		screenFilePath := filepath.Join(packageDir, filepath.FromSlash(s.File))
		screenData, err := os.ReadFile(screenFilePath)
		if err != nil {
			errs = append(errs, fmt.Sprintf("%s file %q not found or unreadable: %v", prefix, s.File, err))
			continue
		}

		var disp display.Display
		if err := json.Unmarshal(screenData, &disp); err != nil {
			errs = append(errs, fmt.Sprintf("%s file %q contains invalid JSON: %v", prefix, s.File, err))
			continue
		}

		if err := display.Validate(&disp); err != nil {
			errs = append(errs, fmt.Sprintf("%s file %q failed display validation: %v", prefix, s.File, err))
		}

		// Validate pose if specified
		if s.Pose != nil {
			if len(s.Pose.Position) != 3 {
				errs = append(errs, fmt.Sprintf("%s pose.position must have exactly 3 coordinates [x, y, z]", prefix))
			}
			if s.Pose.WidthM <= 0 {
				errs = append(errs, fmt.Sprintf("%s pose.widthM must be greater than 0", prefix))
			}
			if s.Pose.HeightM <= 0 {
				errs = append(errs, fmt.Sprintf("%s pose.heightM must be greater than 0", prefix))
			}
		}

		// Validate contexts
		for _, ctx := range s.Contexts {
			if !ValidContexts[ctx] {
				errs = append(errs, fmt.Sprintf("%s has unknown context %q", prefix, ctx))
			}
		}
		for _, ctx := range s.ExcludeContexts {
			if !ValidContexts[ctx] {
				errs = append(errs, fmt.Sprintf("%s has unknown excludeContext %q", prefix, ctx))
			}
		}
	}

	return errs
}

// ValidateCompanions validates companion executable declarations per §4.2.
func ValidateCompanions(m *Manifest) []string {
	var errs []string
	seenCompanionIDs := make(map[string]bool, len(m.Companions))

	for idx, comp := range m.Companions {
		prefix := fmt.Sprintf("companions[%d]", idx)
		if comp.ID == "" {
			errs = append(errs, fmt.Sprintf("%s has empty id", prefix))
		} else {
			prefix = fmt.Sprintf("companion[%q]", comp.ID)
			if seenCompanionIDs[comp.ID] {
				errs = append(errs, fmt.Sprintf("duplicate companion id %q", comp.ID))
			}
			seenCompanionIDs[comp.ID] = true
		}

		if comp.Exe == "" {
			errs = append(errs, fmt.Sprintf("%s has empty exe path", prefix))
		} else if !IsExePathSafe(comp.Exe) {
			errs = append(errs, fmt.Sprintf("%s exe path %q is unsafe (must not escape package folder)", prefix, comp.Exe))
		}

		if comp.Restart != "" && !ValidRestartPolicies[comp.Restart] {
			errs = append(errs, fmt.Sprintf("%s invalid restart policy %q (must be 'on-failure', 'always', or 'never')", prefix, comp.Restart))
		}
	}

	return errs
}
