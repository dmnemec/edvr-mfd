package elite

import (
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
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

// ListJournalFiles lists all Journal.*.log files in dir sorted in chronological ascending order (oldest first).
func ListJournalFiles(dir string) ([]string, error) {
	entries, err := os.ReadDir(dir)
	if err != nil {
		return nil, err
	}

	var files []string
	for _, entry := range entries {
		if entry.IsDir() {
			continue
		}
		name := entry.Name()
		if IsJournalFile(name) {
			files = append(files, filepath.Join(dir, name))
		}
	}

	// Lexicographical sorting matches chronological order for Journal.YYYY-MM-DDTHHMMSS.01.log
	// and Journal.YYMMDDHHMMSS.01.log formats.
	sort.Strings(files)
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
