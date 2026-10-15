package main

import (
	"os"
	"path/filepath"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/env"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/writer"
)

// Config configures the Exploration companion runner.
type Config struct {
	PackageDir            string
	JournalDir            string
	SystemsDir            string
	EventsPath            string
	ExplorationScreenFile string
	CoalesceRate          time.Duration
	PollInterval          time.Duration
}

// DefaultSystemsDir returns %LOCALAPPDATA%\EDVR\mfd-companion\systems per §12.2.
func DefaultSystemsDir() string {
	if p := os.Getenv("EDVR_MFD_SYSTEMS_DIR"); p != "" {
		return p
	}
	localAppData := os.Getenv("LOCALAPPDATA")
	if localAppData == "" {
		if home, err := os.UserHomeDir(); err == nil {
			localAppData = filepath.Join(home, "AppData", "Local")
		}
	}
	return filepath.Join(localAppData, "EDVR", "mfd-companion", "systems")
}

// DefaultConfig returns default configuration resolved from environment and standard paths.
func DefaultConfig() Config {
	pkgDir := env.PackageDir()
	jDir, _ := elite.DiscoverJournalDir()
	envCfg := env.Read()

	return Config{
		PackageDir:            pkgDir,
		JournalDir:            jDir,
		SystemsDir:            DefaultSystemsDir(),
		EventsPath:            envCfg.EventsPath,
		ExplorationScreenFile: env.ScreenPath(pkgDir, "screens/exploration.json"),
		CoalesceRate:          writer.DefaultCoalesceInterval,
		PollInterval:          50 * time.Millisecond,
	}
}
