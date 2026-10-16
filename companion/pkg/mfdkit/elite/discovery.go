package elite

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"time"
)

// EnvJournalDir is the environment variable used to override the default Elite Dangerous journal directory.
const EnvJournalDir = "EDVR_MFD_JOURNAL_DIR"

// ErrNoJournalFound is returned when no journal files match the expected pattern.
var ErrNoJournalFound = errors.New("no journal files found")

// DefaultJournalDir returns the standard Elite Dangerous journal directory under the user's profile:
// %USERPROFILE%\Saved Games\Frontier Developments\Elite Dangerous
func DefaultJournalDir() (string, error) {
	userProfile := os.Getenv("USERPROFILE")
	if userProfile == "" {
		home, err := os.UserHomeDir()
		if err != nil {
			return "", fmt.Errorf("unable to determine user home directory: %w", err)
		}
		userProfile = home
	}
	return filepath.Join(userProfile, "Saved Games", "Frontier Developments", "Elite Dangerous"), nil
}

// DiscoverJournalDir discovers the Elite Dangerous journal directory.
// It checks EDVR_MFD_JOURNAL_DIR first, and falls back to DefaultJournalDir().
func DiscoverJournalDir() (string, error) {
	if dir := os.Getenv(EnvJournalDir); dir != "" {
		return dir, nil
	}
	return DefaultJournalDir()
}

// IsJournalFile returns true if the filename matches Elite Dangerous journal naming (Journal.*.log).
func IsJournalFile(name string) bool {
	return strings.HasPrefix(name, "Journal.") && strings.HasSuffix(name, ".log")
}

// ParseJournalTimestamp extracts the timestamp from a journal filename.
// It supports modern ISO format (e.g., Journal.2026-10-16T001949.01.log)
// and legacy 2022 format (e.g., Journal.221117191657.01.log).
func ParseJournalTimestamp(name string) (time.Time, bool) {
	base := filepath.Base(name)
	parts := strings.Split(base, ".")
	if len(parts) >= 3 && parts[0] == "Journal" {
		tsStr := parts[1]
		layouts := []string{
			"2006-01-02T150405",
			"060102150405",
			time.RFC3339,
			"2006-01-02",
		}
		for _, layout := range layouts {
			if t, err := time.Parse(layout, tsStr); err == nil {
				return t, true
			}
		}
	}
	return time.Time{}, false
}

// ListJournalFiles lists all Journal.*.log files in dir sorted in chronological ascending order (oldest first).
// Files are sorted primarily by file modification time (ModTime). If modification times are identical
// (e.g. rapid writes in tests), it falls back to parsed filename timestamps or lexicographical order.
func ListJournalFiles(dir string) ([]string, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}

	type journalItem struct {
		path      string
		name      string
		modTime   time.Time
		timestamp time.Time
		hasTs     bool
	}

	var items []journalItem
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		if !IsJournalFile(name) {
			continue
		}
		fullPath := filepath.Join(dir, name)
		var modTime time.Time
		if info, err := entry.Info(); err == nil {
			modTime = info.ModTime()
		} else if fi, statErr := os.Stat(fullPath); statErr == nil {
			modTime = fi.ModTime()
		}

		ts, hasTs := ParseJournalTimestamp(name)
		if modTime.IsZero() && hasTs {
			modTime = ts
		}

		items = append(items, journalItem{
			path:      fullPath,
			name:      name,
			modTime:   modTime,
			timestamp: ts,
			hasTs:     hasTs,
		})
	}

	sort.Slice(items, func(i, j int) bool {
		if !items[i].modTime.Equal(items[j].modTime) {
			return items[i].modTime.Before(items[j].modTime)
		}
		if items[i].hasTs && items[j].hasTs {
			if !items[i].timestamp.Equal(items[j].timestamp) {
				return items[i].timestamp.Before(items[j].timestamp)
			}
		}
		return items[i].name < items[j].name
	})

	files := make([]string, len(items))
	for i, it := range items {
		files[i] = it.path
	}
	return files, nil
}

// FindLatestJournal returns the newest Journal.*.log in dir, or ErrNoJournalFound if none exist.
func FindLatestJournal(dir string) (string, error) {
	files, err := ListJournalFiles(dir)
	if err != nil {
		return "", err
	}
	if len(files) == 0 {
		return "", ErrNoJournalFound
	}
	return files[len(files)-1], nil
}
