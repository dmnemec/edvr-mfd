package replay

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
)

// RecordConfig holds parameters for a recording session.
type RecordConfig struct {
	JournalDir   string        // Source Elite Dangerous journal directory
	OutputDir    string        // Destination directory for session files
	SessionID    string        // Unique identifier for this session
	Commander    string        // Commander name override
	Notes        string        // Notes or flight plan description
	PollInterval time.Duration // File polling interval (default: 50ms)
	Duration     time.Duration // Maximum recording duration (0 = until canceled)
}

// Recorder captures live game files and emits a timestamped replay session (§13.2).
type Recorder struct {
	cfg RecordConfig
}

// NewRecorder creates a new Recorder instance with the given config.
func NewRecorder(cfg RecordConfig) *Recorder {
	if cfg.PollInterval <= 0 {
		cfg.PollInterval = 50 * time.Millisecond
	}
	return &Recorder{cfg: cfg}
}

// Record executes the recording process until ctx is canceled or cfg.Duration elapses.
func (r *Recorder) Record(ctx context.Context) (*SessionMetadata, error) {
	if r.cfg.OutputDir == "" {
		return nil, errors.New("output directory is required")
	}

	journalDir := r.cfg.JournalDir
	if journalDir == "" {
		discovered, err := elite.DiscoverJournalDir()
		if err != nil {
			return nil, fmt.Errorf("failed to discover journal directory: %w", err)
		}
		journalDir = discovered
	}

	if err := os.MkdirAll(r.cfg.OutputDir, 0755); err != nil {
		return nil, fmt.Errorf("failed to create session output directory %q: %w", r.cfg.OutputDir, err)
	}

	sessionID := r.cfg.SessionID
	startTime := time.Now().UTC()
	if sessionID == "" {
		sessionID = fmt.Sprintf("session-%s", startTime.Format("20060102-150405"))
	}

	meta := &SessionMetadata{
		SessionID:    sessionID,
		StartTime:    startTime,
		Commander:    r.cfg.Commander,
		Notes:        r.cfg.Notes,
		JournalFiles: make([]string, 0),
	}

	timelinePath := filepath.Join(r.cfg.OutputDir, "timeline.jsonl")
	timelineFile, err := os.OpenFile(timelinePath, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0644)
	if err != nil {
		return nil, fmt.Errorf("failed to create timeline log: %w", err)
	}
	defer timelineFile.Close()

	writeTimelineEntry := func(entry TimelineEntry) error {
		meta.EventsCount++
		data, err := json.Marshal(entry)
		if err != nil {
			return err
		}
		data = append(data, '\n')
		_, err = timelineFile.Write(data)
		return err
	}

	cachedSnapshots := make(map[string][]byte)
	var activeJournalPath string
	var lastJournalOffset int64 = 0

	// 1. Initial snapshot of active journal file
	latestJournal, err := elite.FindLatestJournal(journalDir)
	if err == nil {
		activeJournalPath = latestJournal
		baseName := filepath.Base(latestJournal)
		meta.JournalFiles = append(meta.JournalFiles, baseName)

		initialBytes, rErr := os.ReadFile(latestJournal)
		if rErr == nil && len(initialBytes) > 0 {
			lastJournalOffset = int64(len(initialBytes))
			_ = writeTimelineEntry(TimelineEntry{
				TimeOffsetMs: 0,
				Type:         EntryJournalAppend,
				File:         baseName,
				Content:      string(initialBytes),
			})

			// Copy initial journal to session dir
			_ = os.WriteFile(filepath.Join(r.cfg.OutputDir, baseName), initialBytes, 0644)

			if meta.Commander == "" {
				meta.Commander = extractCommander(initialBytes)
			}
		}
	}

	// 2. Initial snapshot of Status.json, Cargo.json, etc.
	for _, name := range SnapshotFileNames {
		srcPath := filepath.Join(journalDir, name)
		content, rErr := os.ReadFile(srcPath)
		if rErr == nil && len(content) > 0 {
			cachedSnapshots[name] = content
			_ = writeTimelineEntry(TimelineEntry{
				TimeOffsetMs: 0,
				Type:         EntrySnapshot,
				File:         name,
				Content:      string(content),
			})
			_ = os.WriteFile(filepath.Join(r.cfg.OutputDir, name), content, 0644)
		}
	}

	// Write initial session metadata
	writeSessionMetadata(r.cfg.OutputDir, meta)

	ticker := time.NewTicker(r.cfg.PollInterval)
	defer ticker.Stop()

	// 3. Live recording loop
	for {
		select {
		case <-ctx.Done():
			meta.EndTime = time.Now().UTC()
			writeSessionMetadata(r.cfg.OutputDir, meta)
			return meta, nil
		case <-ticker.C:
			if r.cfg.Duration > 0 && time.Since(startTime) >= r.cfg.Duration {
				meta.EndTime = time.Now().UTC()
				writeSessionMetadata(r.cfg.OutputDir, meta)
				return meta, nil
			}

			offsetMs := time.Since(startTime).Milliseconds()

			// Check for Journal rollover
			latest, err := elite.FindLatestJournal(journalDir)
			if err == nil && latest > activeJournalPath {
				activeJournalPath = latest
				lastJournalOffset = 0
				baseName := filepath.Base(latest)
				meta.JournalFiles = append(meta.JournalFiles, baseName)
			}

			// Read newly appended journal bytes
			if activeJournalPath != "" {
				if fi, err := os.Stat(activeJournalPath); err == nil {
					if fi.Size() > lastJournalOffset {
						f, oErr := os.Open(activeJournalPath)
						if oErr == nil {
							_, _ = f.Seek(lastJournalOffset, io.SeekStart)
							newBytes, rErr := io.ReadAll(f)
							f.Close()

							if rErr == nil && len(newBytes) > 0 {
								lastJournalOffset = fi.Size()
								baseName := filepath.Base(activeJournalPath)

								_ = writeTimelineEntry(TimelineEntry{
									TimeOffsetMs: offsetMs,
									Type:         EntryJournalAppend,
									File:         baseName,
									Content:      string(newBytes),
								})

								// Append to session copy
								destJ, aErr := os.OpenFile(filepath.Join(r.cfg.OutputDir, baseName), os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
								if aErr == nil {
									_, _ = destJ.Write(newBytes)
									destJ.Close()
								}

								if meta.Commander == "" {
									if cmdr := extractCommander(newBytes); cmdr != "" {
										meta.Commander = cmdr
									}
								}
							}
						}
					}
				}
			}

			// Check snapshot files for modifications
			for _, name := range SnapshotFileNames {
				srcPath := filepath.Join(journalDir, name)
				content, rErr := os.ReadFile(srcPath)
				if rErr == nil && len(content) > 0 {
					cached := cachedSnapshots[name]
					if !bytes.Equal(content, cached) {
						cachedSnapshots[name] = content

						_ = writeTimelineEntry(TimelineEntry{
							TimeOffsetMs: offsetMs,
							Type:         EntrySnapshot,
							File:         name,
							Content:      string(content),
						})

						_ = os.WriteFile(filepath.Join(r.cfg.OutputDir, name), content, 0644)
					}
				}
			}
		}
	}
}

func writeSessionMetadata(dir string, meta *SessionMetadata) {
	path := filepath.Join(dir, "session.json")
	data, err := json.MarshalIndent(meta, "", "  ")
	if err == nil {
		_ = os.WriteFile(path, data, 0644)
	}
}

func extractCommander(data []byte) string {
	lines := bytes.Split(data, []byte("\n"))
	for _, line := range lines {
		trimmed := bytes.TrimSpace(line)
		if len(trimmed) == 0 {
			continue
		}
		var m map[string]any
		if err := json.Unmarshal(trimmed, &m); err == nil {
			if ev, ok := m["event"].(string); ok {
				if ev == "Commander" {
					if name, ok := m["Name"].(string); ok && name != "" {
						return name
					}
				} else if ev == "LoadGame" {
					if name, ok := m["Commander"].(string); ok && name != "" {
						return name
					}
				}
			}
		}
	}
	return ""
}
