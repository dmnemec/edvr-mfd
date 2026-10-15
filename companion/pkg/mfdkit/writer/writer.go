package writer

import (
	"bytes"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
)

// DefaultCoalesceInterval is the default throttling rate per screen (§11.2).
const DefaultCoalesceInterval = 100 * time.Millisecond

var tmpCounter uint64

// WriteScreen validates the display model, serializes it to indented JSON,
// compares it with any existing file contents on disk (skipping write if identical),
// and atomically overwrites destPath via a temporary file in the same directory.
//
// Returns (wrote bool, err error):
// - (true, nil): contents changed and were written to destPath.
// - (false, nil): contents were identical to existing disk file; write was skipped.
// - (false, err): validation, serialization, or file I/O failed.
func WriteScreen(destPath string, d *display.Display) (bool, error) {
	if destPath == "" {
		return false, errors.New("destPath cannot be empty")
	}
	if d == nil {
		return false, errors.New("display cannot be nil")
	}

	if err := display.Validate(d); err != nil {
		return false, fmt.Errorf("validate display: %w", err)
	}

	data, err := json.MarshalIndent(d, "", "  ")
	if err != nil {
		return false, fmt.Errorf("marshal display JSON: %w", err)
	}
	data = append(data, '\n')

	// Compare with existing file contents if present
	if existing, err := os.ReadFile(destPath); err == nil {
		if bytes.Equal(existing, data) {
			return false, nil
		}
	}

	dir := filepath.Dir(destPath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return false, fmt.Errorf("create directory %s: %w", dir, err)
	}

	// Atomic write: write to temporary file in the same directory, then rename.
	tmpPath := nextTempPath(destPath)
	tmpFile, err := os.OpenFile(tmpPath, os.O_WRONLY|os.O_CREATE|os.O_TRUNC, 0644)
	if err != nil {
		return false, fmt.Errorf("create temp file %s: %w", tmpPath, err)
	}

	_, writeErr := tmpFile.Write(data)
	syncErr := tmpFile.Sync()
	closeErr := tmpFile.Close()

	if writeErr != nil {
		_ = os.Remove(tmpPath)
		return false, fmt.Errorf("write temp file %s: %w", tmpPath, writeErr)
	}
	if syncErr != nil {
		_ = os.Remove(tmpPath)
		return false, fmt.Errorf("sync temp file %s: %w", tmpPath, syncErr)
	}
	if closeErr != nil {
		_ = os.Remove(tmpPath)
		return false, fmt.Errorf("close temp file %s: %w", tmpPath, closeErr)
	}

	if err := atomicRename(tmpPath, destPath); err != nil {
		_ = os.Remove(tmpPath)
		return false, fmt.Errorf("atomic rename %s to %s: %w", tmpPath, destPath, err)
	}

	return true, nil
}

func nextTempPath(destPath string) string {
	dir := filepath.Dir(destPath)
	base := filepath.Base(destPath)
	seq := atomic.AddUint64(&tmpCounter, 1)
	tmpName := fmt.Sprintf("%s.tmp.%d.%d.%d", base, os.Getpid(), time.Now().UnixNano(), seq)
	return filepath.Join(dir, tmpName)
}

func atomicRename(src, dst string) error {
	var err error
	for i := 0; i < 5; i++ {
		err = os.Rename(src, dst)
		if err == nil {
			return nil
		}
		// On Windows or busy filesystems, wait briefly and retry on transient lock
		time.Sleep(5 * time.Millisecond)
	}
	return err
}

// Option configures a CoalescedWriter.
type Option func(*CoalescedWriter)

// WithInterval sets the minimum coalescing interval per screen.
func WithInterval(d time.Duration) Option {
	return func(w *CoalescedWriter) {
		w.interval = d
	}
}

// WithErrorHandler sets an asynchronous error callback.
func WithErrorHandler(fn func(destPath string, err error)) Option {
	return func(w *CoalescedWriter) {
		w.errHandler = fn
	}
}

// WithWriteHook sets a callback invoked after each write attempt (useful for testing and metrics).
func WithWriteHook(fn func(destPath string, wrote bool, err error)) Option {
	return func(w *CoalescedWriter) {
		w.writeHook = fn
	}
}

type screenState struct {
	destPath    string
	mu          sync.Mutex
	pending     *display.Display
	hasPending  bool
	writing     bool
	timerActive bool
	timer       *time.Timer
	lastWrite   time.Time
	cond        *sync.Cond
}

// CoalescedWriter buffers and rate-limits display writes per screen to prevent
// excessive disk I/O while guaranteeing that the latest queued display state is persisted.
type CoalescedWriter struct {
	mu         sync.Mutex
	screens    map[string]*screenState
	interval   time.Duration
	errHandler func(destPath string, err error)
	writeHook  func(destPath string, wrote bool, err error)
	closed     bool
}

// NewCoalescedWriter creates a new CoalescedWriter with optional configuration.
func NewCoalescedWriter(opts ...Option) *CoalescedWriter {
	w := &CoalescedWriter{
		screens:  make(map[string]*screenState),
		interval: DefaultCoalesceInterval,
	}
	for _, opt := range opts {
		opt(w)
	}
	if w.interval <= 0 {
		w.interval = DefaultCoalesceInterval
	}
	return w
}

func (w *CoalescedWriter) getScreenState(destPath string) (*screenState, error) {
	w.mu.Lock()
	defer w.mu.Unlock()

	if w.closed {
		return nil, errors.New("coalesced writer is closed")
	}

	cleanPath := filepath.Clean(destPath)
	s, exists := w.screens[cleanPath]
	if !exists {
		s = &screenState{
			destPath: cleanPath,
		}
		s.cond = sync.NewCond(&s.mu)
		w.screens[cleanPath] = s
	}
	return s, nil
}

// Queue submits a display update for destPath.
// It is non-blocking, validates the model synchronously, and coalesces updates
// to not exceed the configured rate (default <= 1 write per 100 ms per screen).
func (w *CoalescedWriter) Queue(destPath string, d *display.Display) error {
	if destPath == "" {
		return errors.New("destPath cannot be empty")
	}
	if d == nil {
		return errors.New("display cannot be nil")
	}
	if err := display.Validate(d); err != nil {
		return fmt.Errorf("validate display: %w", err)
	}

	cloned := cloneDisplay(d)

	s, err := w.getScreenState(destPath)
	if err != nil {
		return err
	}

	s.mu.Lock()
	defer s.mu.Unlock()

	s.pending = cloned
	s.hasPending = true

	// If a write is in progress or a timer is already scheduled, the latest
	// pending display will be flushed when that completes or triggers.
	if s.writing || s.timerActive {
		return nil
	}

	elapsed := time.Since(s.lastWrite)
	var delay time.Duration
	if !s.lastWrite.IsZero() && elapsed < w.interval {
		delay = w.interval - elapsed
	}

	s.timerActive = true
	s.timer = time.AfterFunc(delay, func() {
		w.processScreen(s)
	})

	return nil
}

func (w *CoalescedWriter) processScreen(s *screenState) {
	s.mu.Lock()
	s.timerActive = false

	if !s.hasPending || s.writing {
		s.mu.Unlock()
		return
	}

	toWrite := s.pending
	s.pending = nil
	s.hasPending = false
	s.writing = true
	s.mu.Unlock()

	wrote, err := WriteScreen(s.destPath, toWrite)
	if err != nil && w.errHandler != nil {
		w.errHandler(s.destPath, err)
	}
	if w.writeHook != nil {
		w.writeHook(s.destPath, wrote, err)
	}

	s.mu.Lock()
	s.lastWrite = time.Now()
	s.writing = false

	// If updates arrived while writing, schedule the next throttled flush.
	if s.hasPending && !s.timerActive {
		s.timerActive = true
		s.timer = time.AfterFunc(w.interval, func() {
			w.processScreen(s)
		})
	}

	s.cond.Broadcast()
	s.mu.Unlock()
}

func (s *screenState) flush(w *CoalescedWriter) error {
	s.mu.Lock()
	defer s.mu.Unlock()

	for s.writing || s.hasPending {
		if !s.writing && s.hasPending {
			if s.timerActive && s.timer != nil {
				s.timer.Stop()
				s.timerActive = false
			}

			toWrite := s.pending
			s.pending = nil
			s.hasPending = false
			s.writing = true
			s.mu.Unlock()

			wrote, err := WriteScreen(s.destPath, toWrite)
			if err != nil && w.errHandler != nil {
				w.errHandler(s.destPath, err)
			}
			if w.writeHook != nil {
				w.writeHook(s.destPath, wrote, err)
			}

			s.mu.Lock()
			s.lastWrite = time.Now()
			s.writing = false
			s.cond.Broadcast()
			if err != nil {
				return err
			}
			continue
		}
		s.cond.Wait()
	}
	return nil
}

// Flush blocks until all pending queued updates across all screens have been written to disk.
func (w *CoalescedWriter) Flush() error {
	w.mu.Lock()
	screens := make([]*screenState, 0, len(w.screens))
	for _, s := range w.screens {
		screens = append(screens, s)
	}
	w.mu.Unlock()

	var firstErr error
	for _, s := range screens {
		if err := s.flush(w); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	return firstErr
}

// FlushScreen blocks until all pending queued updates for destPath have been written to disk.
func (w *CoalescedWriter) FlushScreen(destPath string) error {
	cleanPath := filepath.Clean(destPath)
	w.mu.Lock()
	s, exists := w.screens[cleanPath]
	w.mu.Unlock()

	if !exists {
		return nil
	}
	return s.flush(w)
}

// Close flushes all pending updates and shuts down the writer.
// Subsequent calls to Queue will return an error.
func (w *CoalescedWriter) Close() error {
	w.mu.Lock()
	if w.closed {
		w.mu.Unlock()
		return nil
	}
	w.closed = true
	w.mu.Unlock()

	return w.Flush()
}

func cloneDisplay(d *display.Display) *display.Display {
	if d == nil {
		return nil
	}
	cp := *d
	if d.StatusBadge != nil {
		badge := *d.StatusBadge
		cp.StatusBadge = &badge
	}
	if d.Tabs != nil {
		cp.Tabs = make([]display.Tab, len(d.Tabs))
		for i, tab := range d.Tabs {
			tCp := tab
			if tab.Items != nil {
				tCp.Items = make([]display.Item, len(tab.Items))
				for j, item := range tab.Items {
					iCp := item
					if item.Checks != nil {
						iCp.Checks = make([]display.Check, len(item.Checks))
						copy(iCp.Checks, item.Checks)
					}
					tCp.Items[j] = iCp
				}
			}
			if tab.KeyValues != nil {
				tCp.KeyValues = make([]display.KeyValue, len(tab.KeyValues))
				copy(tCp.KeyValues, tab.KeyValues)
			}
			if tab.Bars != nil {
				tCp.Bars = make([]display.Bar, len(tab.Bars))
				copy(tCp.Bars, tab.Bars)
			}
			cp.Tabs[i] = tCp
		}
	}
	return &cp
}
