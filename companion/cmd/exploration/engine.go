package main

import (
	"context"
	"fmt"
	"log"
	"sync"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/events"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/writer"
)

// Engine coordinates telemetry streams, exploration state, and throttled screen publishing (§12.2).
type Engine struct {
	cfg    Config
	writer *writer.CoalescedWriter
	state  *SystemState
	mu     sync.Mutex
}

// NewEngine initializes an Exploration companion engine.
func NewEngine(cfg Config, w *writer.CoalescedWriter) *Engine {
	if w == nil {
		w = writer.NewCoalescedWriter(writer.WithInterval(cfg.CoalesceRate))
	}
	st := NewSystemState(cfg.SystemsDir)

	return &Engine{
		cfg:    cfg,
		writer: w,
		state:  st,
	}
}

// State returns the current SystemState.
func (e *Engine) State() *SystemState {
	return e.state
}

// FlushScreens forces an immediate regeneration and write of both SYSTEM and BIO screens.
func (e *Engine) FlushScreens() error {
	e.mu.Lock()
	defer e.mu.Unlock()

	sysDisp := e.state.BuildSystemDisplay()
	bioDisp := e.state.BuildBioDisplay()

	_ = e.writer.Queue(e.cfg.SystemScreenFile, sysDisp)
	_ = e.writer.Queue(e.cfg.BioScreenFile, bioDisp)

	return e.writer.Flush()
}

// Run starts the tailer and watchers and runs until ctx is cancelled.
func (e *Engine) Run(ctx context.Context) error {
	defer e.writer.Close()

	if e.cfg.JournalDir == "" {
		return fmt.Errorf("journal directory is required")
	}

	// 1. Start Journal tailer with startup replay
	journalEntries, _ := elite.TailJournal(ctx, e.cfg.JournalDir,
		elite.WithJournalPollInterval(e.cfg.PollInterval),
		elite.WithReplayStartup(true),
	)

	// 2. Start Status.json snapshot watcher
	watchOpts := []elite.WatcherOption{
		elite.WithWatchPollInterval(e.cfg.PollInterval),
		elite.WithWatchInitialRead(true),
	}
	statusCh, _ := elite.WatchStatus(ctx, e.cfg.JournalDir, watchOpts...)

	// 3. Start events.jsonl tailer
	evCh, _ := events.Tail(ctx, e.cfg.EventsPath,
		events.WithPollInterval(e.cfg.PollInterval),
	)

	log.Printf("[exploration] started companion for package %s (journal dir: %s)", e.cfg.PackageDir, e.cfg.JournalDir)

	for {
		select {
		case <-ctx.Done():
			_ = e.state.Save()
			return ctx.Err()

		// Journal stream
		case entry, ok := <-journalEntries:
			if !ok {
				return nil
			}
			sysChanged, bioChanged := e.state.HandleJournalEntry(entry)
			if sysChanged {
				_ = e.writer.Queue(e.cfg.SystemScreenFile, e.state.BuildSystemDisplay())
			}
			if bioChanged {
				_ = e.writer.Queue(e.cfg.BioScreenFile, e.state.BuildBioDisplay())
			}

		// Status.json stream
		case snap, ok := <-statusCh:
			if !ok {
				continue
			}
			sysChanged, bioChanged := e.state.HandleStatus(snap)
			if sysChanged {
				_ = e.writer.Queue(e.cfg.SystemScreenFile, e.state.BuildSystemDisplay())
			}
			if bioChanged {
				_ = e.writer.Queue(e.cfg.BioScreenFile, e.state.BuildBioDisplay())
			}

		// MFD UI interaction events
		case ev, ok := <-evCh:
			if !ok {
				continue
			}
			sysChanged, bioChanged := e.state.HandleEvent(ev)
			if sysChanged {
				_ = e.writer.Queue(e.cfg.SystemScreenFile, e.state.BuildSystemDisplay())
			}
			if bioChanged {
				_ = e.writer.Queue(e.cfg.BioScreenFile, e.state.BuildBioDisplay())
			}
		}
	}
}
