package main

import (
	"context"
	"flag"
	"log"
	"os"
	"os/signal"
	"syscall"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/env"
)

func main() {
	var (
		pkgDir     = flag.String("package", "", "Path to package root (defaults to EDVR_MFD_PACKAGE_DIR)")
		journalDir = flag.String("journal", "", "Path to Elite journal dir (defaults to EDVR_MFD_JOURNAL_DIR)")
		once       = flag.Bool("once", false, "Write initial state and exit immediately")
	)
	flag.Parse()

	// 1. Single-instance named mutex check (§8.2, §12.2)
	mutexName := env.MutexName("exploration", "exploration-feed")
	releaseMutex, acquired, err := env.AcquireSingleInstanceMutex(mutexName)
	if err != nil {
		log.Printf("[exploration] mutex warning: %v", err)
	}
	if !acquired {
		log.Printf("[exploration] another instance is running; exiting cleanly")
		os.Exit(0)
	}
	defer releaseMutex()

	cfg := DefaultConfig()
	if *pkgDir != "" {
		cfg.PackageDir = *pkgDir
		cfg.SystemScreenFile = env.ScreenPath(*pkgDir, "screens/system.json")
		cfg.BioScreenFile = env.ScreenPath(*pkgDir, "screens/bio.json")
	}
	if *journalDir != "" {
		cfg.JournalDir = *journalDir
	}

	if cfg.JournalDir == "" {
		dir, err := elite.DiscoverJournalDir()
		if err != nil {
			log.Fatalf("[exploration] unable to locate journal directory: %v", err)
		}
		cfg.JournalDir = dir
	}

	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()

	eng := NewEngine(cfg, nil)

	if *once {
		if err := eng.FlushScreens(); err != nil {
			log.Fatalf("[exploration] flush failed: %v", err)
		}
		return
	}

	if err := eng.Run(ctx); err != nil && err != context.Canceled {
		log.Fatalf("[exploration] runner terminated: %v", err)
	}
}
