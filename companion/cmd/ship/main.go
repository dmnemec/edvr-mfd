package main

import (
	"context"
	"flag"
	"fmt"
	"log"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/env"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/writer"
)

// Config configures the Ship companion runner.
type Config struct {
	PackageDir      string
	JournalDir      string
	PriceCachePath  string
	CoalesceRate    time.Duration
	PollInterval    time.Duration
	ModulesFile     string
	PowerFile       string
	CargoFile       string
}

// DefaultConfig returns default configuration resolved from environment and standard paths.
func DefaultConfig() Config {
	pkgDir := env.PackageDir()
	jDir, _ := elite.DiscoverJournalDir()

	return Config{
		PackageDir:     pkgDir,
		JournalDir:     jDir,
		PriceCachePath: env.MeanPricesPath(),
		CoalesceRate:   writer.DefaultCoalesceInterval,
		PollInterval:   50 * time.Millisecond,
		ModulesFile:    env.ScreenPath(pkgDir, "screens/modules.json"),
		PowerFile:      env.ScreenPath(pkgDir, "screens/power.json"),
		CargoFile:      env.ScreenPath(pkgDir, "screens/cargo.json"),
	}
}

// Engine coordinates telemetry streams, ship state, and throttled screen publishing.
type Engine struct {
	cfg        Config
	writer     *writer.CoalescedWriter
	priceCache *PriceCache
	state      *ShipState
	mu         sync.Mutex
}

// NewEngine initializes a Ship companion engine.
func NewEngine(cfg Config, w *writer.CoalescedWriter) *Engine {
	if w == nil {
		w = writer.NewCoalescedWriter(writer.WithInterval(cfg.CoalesceRate))
	}
	pc := NewPriceCache(cfg.PriceCachePath)
	st := NewShipState(pc)

	return &Engine{
		cfg:        cfg,
		writer:     w,
		priceCache: pc,
		state:      st,
	}
}

// State returns the current ShipState.
func (e *Engine) State() *ShipState {
	return e.state
}

// FlushScreens forces an immediate regeneration and flush of all 3 screens.
func (e *Engine) FlushScreens() error {
	e.mu.Lock()
	defer e.mu.Unlock()

	modDisp := e.state.BuildModulesDisplay()
	powDisp := e.state.BuildPowerDisplay()
	carDisp := e.state.BuildCargoDisplay()

	_ = e.writer.Queue(e.cfg.ModulesFile, modDisp)
	_ = e.writer.Queue(e.cfg.PowerFile, powDisp)
	_ = e.writer.Queue(e.cfg.CargoFile, carDisp)

	return e.writer.Flush()
}

// Run starts the tailer and watchers and runs until ctx is cancelled.
func (e *Engine) Run(ctx context.Context) error {
	defer e.writer.Close()

	if e.cfg.JournalDir == "" {
		return fmt.Errorf("journal directory is required")
	}

	// 1. Start Journal tailer
	journalEntries, journalErrs := elite.TailJournal(ctx, e.cfg.JournalDir,
		elite.WithJournalPollInterval(e.cfg.PollInterval),
		elite.WithReplayStartup(true),
	)

	// 2. Start snapshot watchers
	watchOpts := []elite.WatcherOption{
		elite.WithWatchPollInterval(e.cfg.PollInterval),
		elite.WithWatchInitialRead(true),
	}

	statusCh, statusErrs := elite.WatchStatus(ctx, e.cfg.JournalDir, watchOpts...)
	cargoCh, cargoErrs := elite.WatchCargo(ctx, e.cfg.JournalDir, watchOpts...)
	marketCh, marketErrs := elite.WatchMarket(ctx, e.cfg.JournalDir, watchOpts...)
	modInfoCh, modInfoErrs := elite.WatchModuleInfo(ctx, e.cfg.JournalDir, watchOpts...)

	log.Printf("[ship] started companion for package %s (journal dir: %s)", e.cfg.PackageDir, e.cfg.JournalDir)

	for {
		select {
		case <-ctx.Done():
			return ctx.Err()

		// Journal feed
		case entry, ok := <-journalEntries:
			if !ok {
				return nil
			}
			modChanged, powChanged, carChanged := e.state.HandleJournalEntry(entry)
			if modChanged {
				_ = e.writer.Queue(e.cfg.ModulesFile, e.state.BuildModulesDisplay())
			}
			if powChanged {
				_ = e.writer.Queue(e.cfg.PowerFile, e.state.BuildPowerDisplay())
			}
			if carChanged {
				_ = e.writer.Queue(e.cfg.CargoFile, e.state.BuildCargoDisplay())
			}

		// Status.json feed
		case snap, ok := <-statusCh:
			if !ok {
				continue
			}
			if changed := e.state.HandleStatus(snap); changed {
				_ = e.writer.Queue(e.cfg.PowerFile, e.state.BuildPowerDisplay())
			}

		// Cargo.json feed
		case snap, ok := <-cargoCh:
			if !ok {
				continue
			}
			if changed := e.state.HandleCargo(snap); changed {
				_ = e.writer.Queue(e.cfg.CargoFile, e.state.BuildCargoDisplay())
			}

		// Market.json feed
		case snap, ok := <-marketCh:
			if !ok {
				continue
			}
			if changed := e.state.HandleMarket(snap); changed {
				_ = e.writer.Queue(e.cfg.CargoFile, e.state.BuildCargoDisplay())
			}

		// ModuleInfo.json feed
		case snap, ok := <-modInfoCh:
			if !ok {
				continue
			}
			if changed := e.state.HandleModuleInfo(snap); changed {
				_ = e.writer.Queue(e.cfg.PowerFile, e.state.BuildPowerDisplay())
			}

		// Non-fatal error logs
		case err, ok := <-journalErrs:
			if ok && err != nil {
				log.Printf("[ship] journal error: %v", err)
			}
		case err, ok := <-statusErrs:
			if ok && err != nil {
				log.Printf("[ship] status error: %v", err)
			}
		case err, ok := <-cargoErrs:
			if ok && err != nil {
				log.Printf("[ship] cargo error: %v", err)
			}
		case err, ok := <-marketErrs:
			if ok && err != nil {
				log.Printf("[ship] market error: %v", err)
			}
		case err, ok := <-modInfoErrs:
			if ok && err != nil {
				log.Printf("[ship] moduleinfo error: %v", err)
			}
		}
	}
}

func main() {
	var (
		pkgDir     = flag.String("package", "", "Path to package root (defaults to EDVR_MFD_PACKAGE_DIR)")
		journalDir = flag.String("journal", "", "Path to Elite journal dir (defaults to EDVR_MFD_JOURNAL_DIR)")
		once       = flag.Bool("once", false, "Write initial state and exit immediately")
	)
	flag.Parse()

	cfg := DefaultConfig()
	if *pkgDir != "" {
		cfg.PackageDir = *pkgDir
		cfg.ModulesFile = env.ScreenPath(*pkgDir, "screens/modules.json")
		cfg.PowerFile = env.ScreenPath(*pkgDir, "screens/power.json")
		cfg.CargoFile = env.ScreenPath(*pkgDir, "screens/cargo.json")
	}
	if *journalDir != "" {
		cfg.JournalDir = *journalDir
	}

	if cfg.JournalDir == "" {
		dir, err := elite.DiscoverJournalDir()
		if err != nil {
			log.Fatalf("[ship] unable to locate journal directory: %v", err)
		}
		cfg.JournalDir = dir
	}

	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()

	eng := NewEngine(cfg, nil)

	if *once {
		if err := eng.FlushScreens(); err != nil {
			log.Fatalf("[ship] flush failed: %v", err)
		}
		return
	}

	if err := eng.Run(ctx); err != nil && err != context.Canceled {
		log.Fatalf("[ship] runner terminated: %v", err)
	}
}
