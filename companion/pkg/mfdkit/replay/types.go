package replay

import "time"

// SnapshotFileNames lists the snapshot JSON files monitored and emitted by Elite Dangerous.
var SnapshotFileNames = []string{
	"Status.json",
	"Cargo.json",
	"Market.json",
	"NavRoute.json",
	"ModuleInfo.json",
}

// SessionMetadata represents the metadata saved to session.json (§13.2).
type SessionMetadata struct {
	SessionID    string    `json:"sessionId"`
	StartTime    time.Time `json:"startTime"`
	EndTime      time.Time `json:"endTime,omitempty"`
	Commander    string    `json:"commander,omitempty"`
	Notes        string    `json:"notes,omitempty"`
	JournalFiles []string  `json:"journalFiles,omitempty"`
	EventsCount  int       `json:"eventsCount"`
}

// TimelineEntryType defines the kind of entry in the recorded timeline.
type TimelineEntryType string

const (
	EntrySnapshot      TimelineEntryType = "snapshot"
	EntryJournalAppend TimelineEntryType = "journal_append"
)

// TimelineEntry represents a timestamped file modification in timeline.jsonl.
type TimelineEntry struct {
	TimeOffsetMs int64             `json:"timeOffsetMs"`
	Type         TimelineEntryType `json:"type"`
	File         string            `json:"file"`
	Content      string            `json:"content"`
}
