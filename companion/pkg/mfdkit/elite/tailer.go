package elite

import (
	"bufio"
	"bytes"
	"context"
	"errors"
	"fmt"
	"io"
	"os"
	"time"
)

// JournalOption configures TailJournal behavior.
type JournalOption func(*journalConfig)

type journalConfig struct {
	pollInterval  time.Duration
	replayStartup bool
	replayAll     bool
	startFile     string
	bufferSize    int
}

// WithJournalPollInterval configures the poll interval for new journal lines or rollovers.
func WithJournalPollInterval(d time.Duration) JournalOption {
	return func(c *journalConfig) {
		if d > 0 {
			c.pollInterval = d
		}
	}
}

// WithReplayStartup configures whether to replay from the most recent LoadGame or Location event at startup.
// Enabled by default per §11.2.
func WithReplayStartup(replay bool) JournalOption {
	return func(c *journalConfig) {
		c.replayStartup = replay
	}
}

// WithReplayAll configures whether to replay the entire newest journal file from offset 0 at startup.
func WithReplayAll(replayAll bool) JournalOption {
	return func(c *journalConfig) {
		c.replayAll = replayAll
	}
}

// WithStartFile explicitly specifies the initial journal file to tail, bypassing automatic discovery.
func WithStartFile(path string) JournalOption {
	return func(c *journalConfig) {
		c.startFile = path
	}
}

// WithJournalBufferSize configures the read buffer size.
func WithJournalBufferSize(sz int) JournalOption {
	return func(c *journalConfig) {
		if sz > 0 {
			c.bufferSize = sz
		}
	}
}

func defaultJournalConfig() journalConfig {
	return journalConfig{
		pollInterval:  25 * time.Millisecond,
		replayStartup: true,
		replayAll:     false,
		bufferSize:    64 * 1024,
	}
}

// findReplayCheckpoint scans a journal file to find the byte offset of the last LoadGame or Location event.
// Returns (offset, true, nil) if found, or (0, false, nil) if neither event exists in the file.
func findReplayCheckpoint(filePath string) (int64, bool, error) {
	file, err := os.Open(filePath)
	if err != nil {
		return 0, false, err
	}
	defer file.Close()

	reader := bufio.NewReader(file)
	var currentOffset int64 = 0
	var lastCheckpointOffset int64 = -1

	for {
		lineOffset := currentOffset
		line, err := reader.ReadBytes('\n')
		if len(line) > 0 {
			currentOffset += int64(len(line))
			// Fast byte check for event names before full JSON parse
			trimmed := bytes.TrimSpace(line)
			if bytes.Contains(trimmed, []byte(`"event":"LoadGame"`)) ||
				bytes.Contains(trimmed, []byte(`"event":"Location"`)) ||
				bytes.Contains(trimmed, []byte(`"event": "LoadGame"`)) ||
				bytes.Contains(trimmed, []byte(`"event": "Location"`)) {
				lastCheckpointOffset = lineOffset
			}
		}
		if err != nil {
			if errors.Is(err, io.EOF) {
				break
			}
			return 0, false, err
		}
	}

	if lastCheckpointOffset >= 0 {
		return lastCheckpointOffset, true, nil
	}
	return 0, false, nil
}

// TailJournal tails Elite Dangerous Journal files in real time:
// - Discovers newest Journal.<timestamp>.<part>.log
// - Replays from the most recent LoadGame or Location event forward at startup
// - Tails newly appended lines in real time
// - Follows journal file rollovers seamlessly when a new Journal file is created
// - Dispatches strongly-typed events or JSON mappings
func TailJournal(ctx context.Context, dir string, opts ...JournalOption) (<-chan JournalEntry, <-chan error) {
	cfg := defaultJournalConfig()
	for _, opt := range opts {
		opt(&cfg)
	}

	entriesCh := make(chan JournalEntry, 128)
	errCh := make(chan error, 32)

	go runJournalTailer(ctx, dir, cfg, entriesCh, errCh)

	return entriesCh, errCh
}

func runJournalTailer(
	ctx context.Context,
	dir string,
	cfg journalConfig,
	entriesCh chan<- JournalEntry,
	errCh chan<- error,
) {
	defer close(entriesCh)
	defer close(errCh)

	sendErr := func(err error) {
		select {
		case errCh <- err:
		default:
		}
	}

	sendEntry := func(entry JournalEntry) bool {
		select {
		case entriesCh <- entry:
			return true
		case <-ctx.Done():
			return false
		}
	}

	// 1. Locate starting journal file
	var currentPath string
	for {
		if cfg.startFile != "" {
			currentPath = cfg.startFile
			break
		}
		latest, err := FindLatestJournal(dir)
		if err == nil {
			currentPath = latest
			break
		}
		// Wait for journal file to be created
		select {
		case <-ctx.Done():
			return
		case <-time.After(cfg.pollInterval):
		}
	}

	// 2. Startup replay
	var currentFile *os.File
	var currentOffset int64 = 0

	replayLinesFromFile := func(filePath string, startOffset int64) error {
		f, err := os.Open(filePath)
		if err != nil {
			return err
		}
		defer f.Close()

		if startOffset > 0 {
			if _, err := f.Seek(startOffset, io.SeekStart); err != nil {
				return err
			}
		}

		reader := bufio.NewReader(f)
		for {
			line, err := reader.ReadBytes('\n')
			if len(line) > 0 {
				trimmed := bytes.TrimSpace(line)
				if len(trimmed) > 0 {
					entry, parseErr := ParseJournalEntry(trimmed)
					if parseErr == nil {
						if !sendEntry(entry) {
							return ctx.Err()
						}
					} else {
						sendErr(parseErr)
					}
				}
			}
			if err != nil {
				if errors.Is(err, io.EOF) {
					break
				}
				return err
			}
		}
		return nil
	}

	if cfg.replayStartup {
		if cfg.replayAll {
			if err := replayLinesFromFile(currentPath, 0); err != nil && !errors.Is(err, context.Canceled) {
				sendErr(fmt.Errorf("replay all failed: %w", err))
			}
		} else {
			// Find checkpoint in latest file
			ckpt, found, err := findReplayCheckpoint(currentPath)
			if err != nil {
				sendErr(fmt.Errorf("failed scanning checkpoint in %s: %w", currentPath, err))
			}

			if found {
				if err := replayLinesFromFile(currentPath, ckpt); err != nil && !errors.Is(err, context.Canceled) {
					sendErr(fmt.Errorf("replay from checkpoint failed: %w", err))
				}
			} else {
				// Checkpoint not in latest file; check preceding journal file if available
				files, _ := ListJournalFiles(dir)
				var prevFile string
				for i := len(files) - 1; i >= 0; i-- {
					if files[i] < currentPath {
						prevFile = files[i]
						break
					}
				}

				if prevFile != "" {
					prevCkpt, prevFound, _ := findReplayCheckpoint(prevFile)
					if prevFound {
						_ = replayLinesFromFile(prevFile, prevCkpt)
					}
				}

				// Now replay the entire latest file
				if err := replayLinesFromFile(currentPath, 0); err != nil && !errors.Is(err, context.Canceled) {
					sendErr(fmt.Errorf("replay latest file failed: %w", err))
				}
			}
		}
	}

	// 3. Open latest file for live tailing
	var err error
	currentFile, err = os.Open(currentPath)
	if err != nil {
		sendErr(fmt.Errorf("failed opening journal file %s: %w", currentPath, err))
		return
	}
	defer currentFile.Close()

	// Advance offset to the end of what was replayed/read
	if fi, err := currentFile.Stat(); err == nil {
		currentOffset = fi.Size()
		_, _ = currentFile.Seek(currentOffset, io.SeekStart)
	}

	ticker := time.NewTicker(cfg.pollInterval)
	defer ticker.Stop()

	var remainder []byte
	buf := make([]byte, cfg.bufferSize)

	readAvailableLines := func() bool {
		fi, statErr := currentFile.Stat()
		if statErr != nil {
			sendErr(statErr)
			return true
		}

		// Handle file truncation
		if fi.Size() < currentOffset {
			currentOffset = 0
			remainder = nil
			_, _ = currentFile.Seek(0, io.SeekStart)
		}

		if fi.Size() > currentOffset {
			for {
				n, rErr := currentFile.Read(buf)
				if n > 0 {
					chunk := buf[:n]
					data := append(remainder, chunk...)
					remainder = nil

					for {
						idx := bytes.IndexByte(data, '\n')
						if idx < 0 {
							// Incomplete line remains in buffer
							remainder = data
							break
						}

						line := data[:idx]
						data = data[idx+1:]
						currentOffset += int64(idx + 1)

						trimmed := bytes.TrimSpace(line)
						if len(trimmed) > 0 {
							entry, parseErr := ParseJournalEntry(trimmed)
							if parseErr == nil {
								if !sendEntry(entry) {
									return false
								}
							} else {
								sendErr(parseErr)
							}
						}
					}
				}
				if rErr != nil {
					break
				}
			}
		}
		return true
	}

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			// A. Read any new bytes appended to current file
			if !readAvailableLines() {
				return
			}

			// B. Check for journal file rollover (a newer Journal.*.log in dir)
			if cfg.startFile == "" {
				latest, err := FindLatestJournal(dir)
				if err == nil && latest > currentPath {
					// Read any trailing bytes in current file before closing
					if len(remainder) > 0 {
						trimmed := bytes.TrimSpace(remainder)
						if len(trimmed) > 0 {
							if entry, parseErr := ParseJournalEntry(trimmed); parseErr == nil {
								sendEntry(entry)
							}
						}
						remainder = nil
					}
					currentFile.Close()

					// Switch to the newly rolled journal file
					newFile, openErr := os.Open(latest)
					if openErr != nil {
						sendErr(fmt.Errorf("failed opening rolled journal %s: %w", latest, openErr))
						continue
					}

					currentFile = newFile
					currentPath = latest
					currentOffset = 0

					// Read everything in the new file immediately
					if !readAvailableLines() {
						return
					}
				}
			}
		}
	}
}
