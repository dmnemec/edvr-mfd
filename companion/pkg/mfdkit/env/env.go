package env

import (
	"os"
	"path/filepath"
	"strings"
)

// Environment variable names per §6.2, §8.2, and §12.1.
const (
	EnvPackageDir    = "EDVR_MFD_PACKAGE_DIR"
	EnvJournalDir    = "EDVR_MFD_JOURNAL_DIR"
	EnvSession       = "EDVR_MFD_SESSION"
	EnvEvents        = "EDVR_MFD_EVENTS"
	EnvPluginVersion = "EDVR_MFD_PLUGIN_VERSION"
	EnvPriceCache    = "EDVR_MFD_PRICE_CACHE"
)

// Config holds standard companion environment variables.
type Config struct {
	PackageDir    string
	JournalDir    string
	Session       string
	EventsPath    string
	PluginVersion string
}

// PackageDir returns the configured package directory or "." if unset.
func PackageDir() string {
	if p := os.Getenv(EnvPackageDir); p != "" {
		return p
	}
	return "."
}

// ScreenPath returns the full path to a screen JSON file given a package dir and screen name or file.
func ScreenPath(pkgDir, screenIDOrFile string) string {
	if strings.HasSuffix(screenIDOrFile, ".json") {
		return filepath.Join(pkgDir, screenIDOrFile)
	}
	return filepath.Join(pkgDir, "screens", screenIDOrFile+".json")
}

// MeanPricesPath returns the path to the mean prices cache file (§12.1).
func MeanPricesPath() string {
	if p := os.Getenv(EnvPriceCache); p != "" {
		return p
	}
	localAppData := os.Getenv("LOCALAPPDATA")
	if localAppData == "" {
		if home, err := os.UserHomeDir(); err == nil {
			localAppData = filepath.Join(home, "AppData", "Local")
		}
	}
	return filepath.Join(localAppData, "EDVR", "mfd-companion", "mean_prices.json")
}

// Read reads companion environment configuration, providing sensible defaults when unset.
func Read() Config {
	pkgDir := PackageDir()

	eventsPath := os.Getenv(EnvEvents)
	if eventsPath == "" {
		eventsPath = filepath.Join(pkgDir, "events.jsonl")
	}

	return Config{
		PackageDir:    pkgDir,
		JournalDir:    os.Getenv(EnvJournalDir),
		Session:       os.Getenv(EnvSession),
		EventsPath:    eventsPath,
		PluginVersion: os.Getenv(EnvPluginVersion),
	}
}

// MutexName returns the standard named mutex name for a companion per §8.2:
// Local\edvr_mfd_<package_id>_<companion_id>
func MutexName(packageID, companionID string) string {
	return "Local\\edvr_mfd_" + packageID + "_" + companionID
}
