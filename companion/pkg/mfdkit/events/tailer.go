package events

import (
	"bufio"
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Option configures Tail behavior.
type Option func(*tailerConfig)

type tailerConfig struct {
	pollInterval time.Duration
	bufferSize   int
	replayRolled bool
}

// WithPollInterval configures the poll interval when waiting for file changes or new lines.
func WithPollInterval(d time.Duration) Option {
	return func(c *tailerConfig) {
		if d > 0 {
			c.pollInterval = d
		}
	}
}

// WithBufferSize configures the read buffer size.
func WithBufferSize(sz int) Option {
	return func(c *tailerConfig) {
		if sz > 0 {
			c.bufferSize = sz
		}
	}
}

// WithReplayRolled configures whether to check for and replay events.1.jsonl at startup
// if it belongs to the same game session as the active events.jsonl.
func WithReplayRolled(replay bool) Option {
	return func(c *tailerConfig) {
		c.replayRolled = replay
	}
}

// RolledPath returns the path of the rolled/rotated file for a given event log path.
// For example, "mfds/ship/events.jsonl" -> "mfds/ship/events.1.jsonl".
func RolledPath(path string) string {
	ext := filepath.Ext(path)
	if ext == "" {
		return path + ".1"
	}
	return strings.TrimSuffix(path, ext) + ".1" + ext
}

// Tail follows the event log at path in real time per §6:
// - Follows file appends in real time.
// - Handles file truncation at new game session (detect seq drop or session ID change).
// - Handles log rotation (detects roll from events.jsonl to events.1.jsonl or file replacement).
// - Exposes structured events cleanly.
func Tail(ctx context.Context, path string, opts ...Option) (<-chan Event, <-chan error) {
	cfg := tailerConfig{
		pollInterval: 20 * time.Millisecond,
		bufferSize:   64 * 1024,
		replayRolled: true,
	}
	for _, opt := range opts {
		opt(&cfg)
	}

	eventsCh := make(chan Event, 64)
	errCh := make(chan error, 32)

	go runTailer(ctx, path, cfg, eventsCh, errCh)

	return eventsCh, errCh
}

func runTailer(ctx context.Context, path string, cfg tailerConfig, eventsCh chan<- Event, errCh chan<- error) {
	defer close(eventsCh)
	defer close(errCh)

	sendErr := func(err error) {
		select {
		case errCh <- err:
		default:
		}
	}

	sendEvent := func(ev Event) bool {
		select {
		case eventsCh <- ev:
			return true
		case <-ctx.Done():
			return false
		}
	}

	var (
		lastSeq     uint64
		lastSession string
	)

	// Step 1: Optional replay of rolled file at startup if matching session
	if cfg.replayRolled {
		replayMatchingRolledFile(ctx, path, &lastSeq, &lastSession, sendEvent, sendErr)
	}

	var (
		file    *os.File
		offset  int64
		buf     = make([]byte, cfg.bufferSize)
		pending []byte
	)

	defer func() {
		if file != nil {
			_ = file.Close()
		}
	}()

	openActiveFile := func() bool {
		if file != nil {
			_ = file.Close()
			file = nil
		}
		offset = 0
		pending = nil

		for {
			select {
			case <-ctx.Done():
				return false
			default:
			}

			_, err := os.Stat(path)
			if err != nil {
				select {
				case <-ctx.Done():
					return false
				case <-time.After(cfg.pollInterval):
					continue
				}
			}

			f, err := os.Open(path)
			if err != nil {
				file = nil
				sendErr(fmt.Errorf("events: open %s: %w", path, err))
				select {
				case <-ctx.Done():
					return false
				case <-time.After(cfg.pollInterval):
					continue
				}
			}

			file = f
			return true
		}
	}

	if !openActiveFile() {
		return
	}

	for {
		select {
		case <-ctx.Done():
			return
		default:
		}

		// Read available data from current file
		n, readErr := file.Read(buf)
		if n > 0 {
			offset += int64(n)
			pending = append(pending, buf[:n]...)
			if !drainCompleteLines(&pending, &lastSeq, &lastSession, sendEvent, sendErr) {
				return
			}
		}

		if readErr != nil && !errors.Is(readErr, io.EOF) {
			sendErr(fmt.Errorf("events: read %s: %w", path, readErr))
		}

		// Inspect current status of file path on disk
		pathStat, statErr := os.Stat(path)
		if statErr != nil {
			// File might be in the middle of being rotated or recreated
			select {
			case <-ctx.Done():
				return
			case <-time.After(cfg.pollInterval):
				continue
			}
		}

		currFileStat, currStatErr := file.Stat()
		if currStatErr != nil {
			sendErr(fmt.Errorf("events: file stat %s: %w", path, currStatErr))
			if !openActiveFile() {
				return
			}
			continue
		}

		// 1. Log Rotation or File Replacement:
		// The file at path is a different file from our open file descriptor
		if !os.SameFile(currFileStat, pathStat) {
			if !drainRemainingFile(file, buf, &pending, &offset, &lastSeq, &lastSession, sendEvent, sendErr) {
				return
			}
			if !openActiveFile() {
				return
			}
			continue
		}

		// 2. File Truncation:
		// File identity is unchanged, but file size shrunk below our current read offset
		if pathStat.Size() < offset {
			if _, err := file.Seek(0, io.SeekStart); err != nil {
				sendErr(fmt.Errorf("events: seek after truncation: %w", err))
				if !openActiveFile() {
					return
				}
			} else {
				offset = 0
				pending = nil
				lastSeq = 0
				lastSession = ""
			}
			continue
		}

		// 3. No new data yet: sleep for poll interval
		if n == 0 {
			select {
			case <-ctx.Done():
				return
			case <-time.After(cfg.pollInterval):
			}
		}
	}
}

// drainCompleteLines extracts all complete newline-delimited lines from pending and processes them.
func drainCompleteLines(pending *[]byte, lastSeq *uint64, lastSession *string, sendEvent func(Event) bool, sendErr func(error)) bool {
	for {
		newlineIdx := bytes.IndexByte(*pending, '\n')
		if newlineIdx < 0 {
			break
		}

		line := (*pending)[:newlineIdx]
		*pending = (*pending)[newlineIdx+1:]

		line = bytes.TrimRight(line, "\r")
		if len(bytes.TrimSpace(line)) == 0 {
			continue
		}

		var ev Event
		if err := json.Unmarshal(line, &ev); err != nil {
			sendErr(fmt.Errorf("events: unmarshal JSON line: %w", err))
			continue
		}

		*lastSession = ev.Session
		*lastSeq = ev.Seq

		if !sendEvent(ev) {
			return false
		}
	}
	return true
}

// drainRemainingFile reads and processes any unread bytes from a file descriptor before closing it.
func drainRemainingFile(f *os.File, buf []byte, pending *[]byte, offset *int64, lastSeq *uint64, lastSession *string, sendEvent func(Event) bool, sendErr func(error)) bool {
	for {
		rn, rErr := f.Read(buf)
		if rn > 0 {
			*offset += int64(rn)
			*pending = append(*pending, buf[:rn]...)
			if !drainCompleteLines(pending, lastSeq, lastSession, sendEvent, sendErr) {
				return false
			}
		}
		if rn == 0 || errors.Is(rErr, io.EOF) {
			break
		}
	}

	if len(bytes.TrimSpace(*pending)) > 0 {
		var ev Event
		if err := json.Unmarshal(*pending, &ev); err == nil {
			*lastSession = ev.Session
			*lastSeq = ev.Seq
			if !sendEvent(ev) {
				return false
			}
		}
		*pending = nil
	}
	return true
}

// replayMatchingRolledFile checks if a rolled file (e.g. events.1.jsonl) exists and
// belongs to the same session as events.jsonl. If so, it replays all events from it.
func replayMatchingRolledFile(ctx context.Context, path string, lastSeq *uint64, lastSession *string, sendEvent func(Event) bool, sendErr func(error)) {
	rolled := RolledPath(path)
	rolledStat, err := os.Stat(rolled)
	if err != nil || rolledStat.Size() == 0 {
		return
	}

	liveStat, err := os.Stat(path)
	if err != nil || liveStat.Size() == 0 {
		return
	}

	rolledFirst, err := readFirstEvent(rolled)
	if err != nil {
		return
	}

	liveFirst, err := readFirstEvent(path)
	if err != nil {
		return
	}

	// Must share session and rolled seq must be earlier than live seq
	if rolledFirst.Session == "" || rolledFirst.Session != liveFirst.Session || rolledFirst.Seq >= liveFirst.Seq {
		return
	}

	f, err := os.Open(rolled)
	if err != nil {
		sendErr(fmt.Errorf("events: open rolled %s: %w", rolled, err))
		return
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	buf := make([]byte, 64*1024)
	scanner.Buffer(buf, 10*1024*1024)

	for scanner.Scan() {
		select {
		case <-ctx.Done():
			return
		default:
		}

		line := bytes.TrimRight(scanner.Bytes(), "\r")
		if len(bytes.TrimSpace(line)) == 0 {
			continue
		}

		var ev Event
		if err := json.Unmarshal(line, &ev); err != nil {
			sendErr(fmt.Errorf("events: unmarshal rolled JSON line: %w", err))
			continue
		}

		*lastSession = ev.Session
		*lastSeq = ev.Seq

		if !sendEvent(ev) {
			return
		}
	}

	if err := scanner.Err(); err != nil && !errors.Is(err, io.EOF) {
		sendErr(fmt.Errorf("events: scan rolled %s: %w", rolled, err))
	}
}

// readFirstEvent reads the first non-empty JSON event from a file.
func readFirstEvent(filePath string) (*Event, error) {
	f, err := os.Open(filePath)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := bytes.TrimSpace(scanner.Bytes())
		if len(line) == 0 {
			continue
		}
		var ev Event
		if err := json.Unmarshal(line, &ev); err != nil {
			return nil, err
		}
		return &ev, nil
	}
	if err := scanner.Err(); err != nil {
		return nil, err
	}
	return nil, io.EOF
}
