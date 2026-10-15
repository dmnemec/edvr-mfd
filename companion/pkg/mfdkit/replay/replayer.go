package replay

import (
	"bufio"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"time"
)

// ReplayConfig holds parameters for a replay playback session.
type ReplayConfig struct {
	SessionDir string  // Directory containing recorded session files
	DestDir    string  // Target destination directory mimicking Saved Games / journal directory
	Speed      float64 // Playback speed multiplier (1.0 = 1x, 2.0 = 2x, 0 = instant)
}

// ParseSpeed parses speed multiplier strings such as "1x", "2x", "4x", "instant", "0.5x".
func ParseSpeed(s string) (float64, error) {
	trimmed := strings.ToLower(strings.TrimSpace(s))
	if trimmed == "" || trimmed == "1x" || trimmed == "1" {
		return 1.0, nil
	}
	if trimmed == "2x" || trimmed == "2" {
		return 2.0, nil
	}
	if trimmed == "4x" || trimmed == "4" {
		return 4.0, nil
	}
	if trimmed == "instant" || trimmed == "0" || trimmed == "0x" || trimmed == "max" {
		return 0.0, nil
	}

	trimmed = strings.TrimSuffix(trimmed, "x")
	val, err := strconv.ParseFloat(trimmed, 64)
	if err != nil {
		return 0, fmt.Errorf("invalid speed value %q: %w", s, err)
	}
	if val < 0 {
		return 0, fmt.Errorf("speed must be non-negative: %v", val)
	}
	return val, nil
}

// Replayer emits recorded session files into DestDir at specified speed (§13.2).
type Replayer struct {
	cfg ReplayConfig
}

// NewReplayer creates a new Replayer with the provided config.
func NewReplayer(cfg ReplayConfig) *Replayer {
	return &Replayer{cfg: cfg}
}

// Replay executes the playback timeline.
func (rp *Replayer) Replay(ctx context.Context) error {
	if rp.cfg.SessionDir == "" {
		return errors.New("session directory (--in) is required")
	}
	if rp.cfg.DestDir == "" {
		return errors.New("destination directory (--out) is required")
	}

	if err := os.MkdirAll(rp.cfg.DestDir, 0755); err != nil {
		return fmt.Errorf("failed to create destination directory %q: %w", rp.cfg.DestDir, err)
	}

	timelinePath := filepath.Join(rp.cfg.SessionDir, "timeline.jsonl")
	if _, err := os.Stat(timelinePath); errors.Is(err, os.ErrNotExist) {
		altPath := filepath.Join(rp.cfg.SessionDir, "deltas.jsonl")
		if _, aErr := os.Stat(altPath); aErr == nil {
			timelinePath = altPath
		}
	}

	entries, err := loadTimelineEntries(timelinePath)
	if err != nil {
		// If no timeline exists, fallback to copying static snapshot and journal files directly
		return rp.copyStaticSessionFiles()
	}

	playbackStart := time.Now()

	for _, entry := range entries {
		select {
		case <-ctx.Done():
			return ctx.Err()
		default:
		}

		if rp.cfg.Speed > 0 && entry.TimeOffsetMs > 0 {
			targetDelay := time.Duration(float64(entry.TimeOffsetMs)/rp.cfg.Speed) * time.Millisecond
			elapsed := time.Since(playbackStart)
			if targetDelay > elapsed {
				select {
				case <-ctx.Done():
					return ctx.Err()
				case <-time.After(targetDelay - elapsed):
				}
			}
		}

		if err := rp.applyEntry(entry); err != nil {
			return err
		}
	}

	return nil
}

func (rp *Replayer) applyEntry(entry TimelineEntry) error {
	destPath := filepath.Join(rp.cfg.DestDir, filepath.Base(entry.File))

	switch entry.Type {
	case EntrySnapshot:
		// Mimic Elite Dangerous atomic/direct snapshot writes
		tmpFile := destPath + ".tmp"
		if err := os.WriteFile(tmpFile, []byte(entry.Content), 0644); err != nil {
			return fmt.Errorf("failed writing snapshot %s: %w", tmpFile, err)
		}
		if err := os.Rename(tmpFile, destPath); err != nil {
			// On Windows or platforms where rename over existing might fail, fallback to direct write
			if err := os.WriteFile(destPath, []byte(entry.Content), 0644); err != nil {
				return fmt.Errorf("failed replacing snapshot %s: %w", destPath, err)
			}
			_ = os.Remove(tmpFile)
		}

	case EntryJournalAppend:
		f, err := os.OpenFile(destPath, os.O_CREATE|os.O_WRONLY|os.O_APPEND, 0644)
		if err != nil {
			return fmt.Errorf("failed opening journal for append %s: %w", destPath, err)
		}
		defer f.Close()

		if _, err := f.WriteString(entry.Content); err != nil {
			return fmt.Errorf("failed appending to journal %s: %w", destPath, err)
		}
	}

	return nil
}

func (rp *Replayer) copyStaticSessionFiles() error {
	entries, err := os.ReadDir(rp.cfg.SessionDir)
	if err != nil {
		return err
	}

	for _, entry := range entries {
		if entry.IsDir() || entry.Name() == "session.json" || entry.Name() == "timeline.jsonl" {
			continue
		}
		src := filepath.Join(rp.cfg.SessionDir, entry.Name())
		dst := filepath.Join(rp.cfg.DestDir, entry.Name())
		data, err := os.ReadFile(src)
		if err != nil {
			return err
		}
		if err := os.WriteFile(dst, data, 0644); err != nil {
			return err
		}
	}
	return nil
}

func loadTimelineEntries(path string) ([]TimelineEntry, error) {
	file, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer file.Close()

	var entries []TimelineEntry
	scanner := bufio.NewScanner(file)
	// Buffer large journal chunks
	buf := make([]byte, 1024*1024)
	scanner.Buffer(buf, 10*1024*1024)

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" {
			continue
		}
		var entry TimelineEntry
		if err := json.Unmarshal([]byte(line), &entry); err != nil {
			continue
		}
		entries = append(entries, entry)
	}

	if err := scanner.Err(); err != nil {
		return nil, err
	}

	return entries, nil
}
