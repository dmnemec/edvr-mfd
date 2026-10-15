package replay_test

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/replay"
)

func TestParseSpeed(t *testing.T) {
	tests := []struct {
		input       string
		expected    float64
		expectError bool
	}{
		{"", 1.0, false},
		{"1x", 1.0, false},
		{"1X", 1.0, false},
		{"1", 1.0, false},
		{"2x", 2.0, false},
		{"4x", 4.0, false},
		{"instant", 0.0, false},
		{"0", 0.0, false},
		{"0x", 0.0, false},
		{"max", 0.0, false},
		{"0.5x", 0.5, false},
		{"8x", 8.0, false},
		{"-1x", 0.0, true},
		{"invalid", 0.0, true},
	}

	for _, tt := range tests {
		val, err := replay.ParseSpeed(tt.input)
		if tt.expectError {
			if err == nil {
				t.Errorf("ParseSpeed(%q) expected error, got nil", tt.input)
			}
		} else {
			if err != nil {
				t.Errorf("ParseSpeed(%q) unexpected error: %v", tt.input, err)
			}
			if val != tt.expected {
				t.Errorf("ParseSpeed(%q) expected %v, got %v", tt.input, tt.expected, val)
			}
		}
	}
}

func TestRecordAndReplay_Instant(t *testing.T) {
	sourceDir := filepath.Join(t.TempDir(), "source_journal")
	sessionDir := filepath.Join(t.TempDir(), "recorded_session")
	destDir := filepath.Join(t.TempDir(), "replayed_journal")

	if err := os.MkdirAll(sourceDir, 0755); err != nil {
		t.Fatal(err)
	}

	// 1. Initial source files
	journalName := "Journal.2026-10-15T120000.01.log"
	initialJournal := `{"timestamp":"2026-10-15T12:00:00Z","event":"Commander","Name":"Jameson"}
{"timestamp":"2026-10-15T12:00:01Z","event":"LoadGame","Commander":"Jameson","Ship":"CobraMkIII"}
`
	if err := os.WriteFile(filepath.Join(sourceDir, journalName), []byte(initialJournal), 0644); err != nil {
		t.Fatal(err)
	}

	initialStatus := `{"timestamp":"2026-10-15T12:00:00Z","event":"Status","Flags":16777216}`
	if err := os.WriteFile(filepath.Join(sourceDir, "Status.json"), []byte(initialStatus), 0644); err != nil {
		t.Fatal(err)
	}

	// 2. Start recorder in background
	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	rec := replay.NewRecorder(replay.RecordConfig{
		JournalDir:   sourceDir,
		OutputDir:    sessionDir,
		SessionID:    "test-session-01",
		Notes:        "Unit test recording",
		PollInterval: 20 * time.Millisecond,
	})

	recErrCh := make(chan error, 1)
	var meta *replay.SessionMetadata
	go func() {
		var err error
		meta, err = rec.Record(ctx)
		recErrCh <- err
	}()

	// Wait for recorder initial snapshot
	time.Sleep(60 * time.Millisecond)

	// 3. Simulate game events: append to Journal and update Status.json
	f, err := os.OpenFile(filepath.Join(sourceDir, journalName), os.O_WRONLY|os.O_APPEND, 0644)
	if err != nil {
		t.Fatal(err)
	}
	appendedJournal := `{"timestamp":"2026-10-15T12:00:05Z","event":"SupercruiseEntry","StarSystem":"Sol"}
`
	_, _ = f.WriteString(appendedJournal)
	f.Close()

	updatedStatus := `{"timestamp":"2026-10-15T12:00:05Z","event":"Status","Flags":16777232}`
	if err := os.WriteFile(filepath.Join(sourceDir, "Status.json"), []byte(updatedStatus), 0644); err != nil {
		t.Fatal(err)
	}

	// Allow recorder polling loop to capture updates
	time.Sleep(100 * time.Millisecond)
	cancel()

	if err := <-recErrCh; err != nil {
		t.Fatalf("recorder error: %v", err)
	}

	// 4. Verify session directory contents
	if meta == nil {
		t.Fatal("expected non-nil SessionMetadata")
	}
	if meta.Commander != "Jameson" {
		t.Errorf("expected Commander 'Jameson', got %q", meta.Commander)
	}
	if meta.SessionID != "test-session-01" {
		t.Errorf("expected SessionID 'test-session-01', got %q", meta.SessionID)
	}

	sessionJSONPath := filepath.Join(sessionDir, "session.json")
	sessionData, err := os.ReadFile(sessionJSONPath)
	if err != nil {
		t.Fatalf("failed reading session.json: %v", err)
	}
	var readMeta replay.SessionMetadata
	if err := json.Unmarshal(sessionData, &readMeta); err != nil {
		t.Fatalf("failed parsing session.json: %v", err)
	}
	if readMeta.SessionID != "test-session-01" {
		t.Errorf("session.json sessionID mismatch: %s", readMeta.SessionID)
	}

	timelinePath := filepath.Join(sessionDir, "timeline.jsonl")
	if _, err := os.Stat(timelinePath); err != nil {
		t.Fatalf("timeline.jsonl missing: %v", err)
	}

	// 5. Test Replayer with instant speed
	replayer := replay.NewReplayer(replay.ReplayConfig{
		SessionDir: sessionDir,
		DestDir:    destDir,
		Speed:      0.0, // instant
	})

	replayCtx := context.Background()
	if err := replayer.Replay(replayCtx); err != nil {
		t.Fatalf("replayer failed: %v", err)
	}

	// 6. Verify destDir contents
	replayedJournal, err := os.ReadFile(filepath.Join(destDir, journalName))
	if err != nil {
		t.Fatalf("replayed journal missing: %v", err)
	}
	expectedFullJournal := initialJournal + appendedJournal
	if string(replayedJournal) != expectedFullJournal {
		t.Errorf("replayed journal mismatch.\nExpected:\n%s\nGot:\n%s", expectedFullJournal, string(replayedJournal))
	}

	replayedStatus, err := os.ReadFile(filepath.Join(destDir, "Status.json"))
	if err != nil {
		t.Fatalf("replayed Status.json missing: %v", err)
	}
	if string(replayedStatus) != updatedStatus {
		t.Errorf("replayed Status.json mismatch. Expected %s, got %s", updatedStatus, string(replayedStatus))
	}
}

func TestReplay_Cancellation(t *testing.T) {
	sessionDir := filepath.Join(t.TempDir(), "session")
	destDir := filepath.Join(t.TempDir(), "dest")
	_ = os.MkdirAll(sessionDir, 0755)

	// Create a timeline with future offset
	entries := []replay.TimelineEntry{
		{TimeOffsetMs: 0, Type: replay.EntrySnapshot, File: "Status.json", Content: `{"event":"Status"}`},
		{TimeOffsetMs: 5000, Type: replay.EntrySnapshot, File: "Status.json", Content: `{"event":"Status","updated":true}`},
	}
	timelinePath := filepath.Join(sessionDir, "timeline.jsonl")
	f, _ := os.Create(timelinePath)
	for _, e := range entries {
		data, _ := json.Marshal(e)
		_, _ = f.Write(append(data, '\n'))
	}
	f.Close()

	ctx, cancel := context.WithCancel(context.Background())
	rp := replay.NewReplayer(replay.ReplayConfig{
		SessionDir: sessionDir,
		DestDir:    destDir,
		Speed:      1.0,
	})

	errCh := make(chan error, 1)
	go func() {
		errCh <- rp.Replay(ctx)
	}()

	time.Sleep(50 * time.Millisecond)
	cancel()

	err := <-errCh
	if err == nil || !strings.Contains(err.Error(), "context canceled") {
		t.Fatalf("expected context canceled error, got %v", err)
	}
}
