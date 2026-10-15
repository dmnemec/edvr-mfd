package main

import (
	"context"
	"log"
	"os"
	"os/signal"
	"path/filepath"
	"syscall"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/env"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/events"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/writer"
)

func main() {
	// 1. Single-instance mutex check (§8.2, §11.2)
	mutexName := env.MutexName("route", "route-feed")
	releaseMutex, acquired, err := env.AcquireSingleInstanceMutex(mutexName)
	if err != nil {
		log.Printf("route companion: mutex error: %v", err)
	}
	if !acquired {
		log.Printf("route companion: another instance is running; exiting cleanly")
		os.Exit(0)
	}
	defer releaseMutex()

	// 2. Read environment configuration
	cfg := env.Read()
	journalDir, err := elite.DiscoverJournalDir()
	if err != nil {
		log.Printf("route companion: failed to discover journal dir: %v", err)
	}

	destScreen := env.ScreenPath(cfg.PackageDir, "screens/route.json")
	iniPath := filepath.Join(cfg.PackageDir, "route.ini")
	csvPath := filepath.Join(cfg.PackageDir, "route.csv")

	// 3. Initialize coalesced screen writer (max 1 write per 100ms per §11.2)
	screenWriter := writer.NewCoalescedWriter(
		writer.WithInterval(100*time.Millisecond),
		writer.WithErrorHandler(func(path string, err error) {
			log.Printf("route companion: screen write error for %s: %v", path, err)
		}),
	)
	defer screenWriter.Close()

	// 4. Initialize RouteEngine
	engine := NewRouteEngine(
		csvPath,
		WithWriter(screenWriter, destScreen),
		WithIniPath(iniPath),
	)

	// Publish initial screen state immediately
	engine.mu.Lock()
	engine.syncScreenLocked()
	engine.mu.Unlock()

	// 5. Setup context and signal notification
	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()

	// 6. Watch NavRoute.json
	navCh, navErrCh := elite.WatchNavRoute(ctx, journalDir, elite.WithWatchPollInterval(100*time.Millisecond))

	// 7. Tail Journal
	jCh, jErrCh := elite.TailJournal(ctx, journalDir, elite.WithReplayStartup(true))

	// 8. Tail events.jsonl
	evCh, evErrCh := events.Tail(ctx, cfg.EventsPath)

	// 9. Periodic CSV watcher
	csvTicker := time.NewTicker(2 * time.Second)
	defer csvTicker.Stop()
	var lastCSVMod time.Time
	if fi, err := os.Stat(csvPath); err == nil {
		lastCSVMod = fi.ModTime()
	}

	log.Printf("route companion started: pkg=%s, dest=%s", cfg.PackageDir, destScreen)

	// Main event loop
	for {
		select {
		case <-ctx.Done():
			_ = screenWriter.Flush()
			log.Printf("route companion shutting down")
			return

		case snap, ok := <-navCh:
			if !ok {
				navCh = nil
				continue
			}
			engine.HandleNavRoute(snap)

		case jEntry, ok := <-jCh:
			if !ok {
				jCh = nil
				continue
			}
			switch ev := jEntry.Data.(type) {
			case *elite.EventFSDJump:
				engine.HandleFSDJump(ev.StarSystem)
			case *elite.EventLocation:
				engine.HandleLocation(ev.StarSystem)
			case *elite.EventNavRouteClear:
				engine.HandleNavRouteClear()
			}

		case ev, ok := <-evCh:
			if !ok {
				evCh = nil
				continue
			}
			// Session check (§11.2): exit 0 on session mismatch
			if cfg.Session != "" && ev.Session != "" && cfg.Session != ev.Session {
				log.Printf("route companion: session mismatch (active=%s, event=%s); exiting cleanly",
					cfg.Session, ev.Session)
				_ = screenWriter.Flush()
				os.Exit(0)
			}
			engine.HandleEvent(ev)

		case <-csvTicker.C:
			if fi, err := os.Stat(csvPath); err == nil {
				if fi.ModTime().After(lastCSVMod) {
					lastCSVMod = fi.ModTime()
					engine.ReloadSpansh()
					engine.mu.Lock()
					engine.syncScreenLocked()
					engine.mu.Unlock()
				}
			} else if !lastCSVMod.IsZero() {
				// CSV file was removed
				lastCSVMod = time.Time{}
				engine.ReloadSpansh()
				engine.mu.Lock()
				engine.syncScreenLocked()
				engine.mu.Unlock()
			}

		case err, ok := <-navErrCh:
			if ok && err != nil {
				log.Printf("route companion: nav watcher warning: %v", err)
			}
		case err, ok := <-jErrCh:
			if ok && err != nil {
				log.Printf("route companion: journal tailer warning: %v", err)
			}
		case err, ok := <-evErrCh:
			if ok && err != nil {
				log.Printf("route companion: events tailer warning: %v", err)
			}
		}
	}
}
