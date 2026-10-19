package events_test

import (
	"context"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"sync"
	"testing"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/events"
)

func ptr[T any](v T) *T {
	return &v
}

func TestEvent_JSONMarshalingAndUnmarshaling(t *testing.T) {
	now := time.Now().UTC().Truncate(time.Millisecond)

	tests := []struct {
		name     string
		rawJSON  string
		expected events.Event
	}{
		{
			name:    "select event with flat fields per spec",
			rawJSON: fmt.Sprintf(`{"seq":42,"ts":"%s","session":"sess-1","screen":"modules","type":"select","tab":"integrity","index":3,"itemId":"slot_fsd","label":"Frame Shift Drive","key":null,"delta":null,"action":"toggle"}`, now.Format(time.RFC3339Nano)),
			expected: events.Event{
				Seq:     42,
				TS:      now,
				Session: "sess-1",
				Screen:  "modules",
				Type:    events.TypeSelect,
				Tab:     "integrity",
				Index:   3,
				ItemID:  "slot_fsd",
				Label:   "Frame Shift Drive",
				Action:  "toggle",
			},
		},
		{
			name:    "adjust event with key and delta",
			rawJSON: fmt.Sprintf(`{"seq":43,"ts":"%s","session":"sess-1","screen":"power","type":"adjust","tab":"dist","index":0,"itemId":"sys","key":"sys_pips","delta":1}`, now.Format(time.RFC3339Nano)),
			expected: events.Event{
				Seq:     43,
				TS:      now,
				Session: "sess-1",
				Screen:  "power",
				Type:    events.TypeAdjust,
				Tab:     "dist",
				Index:   0,
				ItemID:  "sys",
				Key:     ptr("sys_pips"),
				Delta:   ptr(1),
			},
		},
		{
			name:    "nav event from a held-direction repeat",
			rawJSON: fmt.Sprintf(`{"seq":44,"ts":"%s","session":"sess-1","screen":"nav","type":"nav","tab":"t","index":3,"itemId":"i3","label":"Item 3","repeat":true,"repeatCount":2}`, now.Format(time.RFC3339Nano)),
			expected: events.Event{
				Seq:         44,
				TS:          now,
				Session:     "sess-1",
				Screen:      "nav",
				Type:        events.TypeNav,
				Tab:         "t",
				Index:       3,
				ItemID:      "i3",
				Label:       "Item 3",
				Repeat:      true,
				RepeatCount: 2,
			},
		},
		{
			name:    "adjust event from a held-direction repeat",
			rawJSON: fmt.Sprintf(`{"seq":45,"ts":"%s","session":"sess-1","screen":"power","type":"adjust","tab":"dist","index":0,"itemId":"sys","key":"sys_pips","delta":1,"repeat":true,"repeatCount":1}`, now.Format(time.RFC3339Nano)),
			expected: events.Event{
				Seq:         45,
				TS:          now,
				Session:     "sess-1",
				Screen:      "power",
				Type:        events.TypeAdjust,
				Tab:         "dist",
				ItemID:      "sys",
				Key:         ptr("sys_pips"),
				Delta:       ptr(1),
				Repeat:      true,
				RepeatCount: 1,
			},
		},
		{
			name:    "load_error with flat fields",
			rawJSON: fmt.Sprintf(`{"seq":2,"ts":"%s","session":"sess-1","type":"load_error","file":"screens/nav.json","line":42,"column":10,"message":"invalid syntax"}`, now.Format(time.RFC3339Nano)),
			expected: events.Event{
				Seq:     2,
				TS:      now,
				Session: "sess-1",
				Type:    events.TypeLoadError,
				ErrorDetails: &events.ErrorDetails{
					File:    "screens/nav.json",
					Line:    42,
					Column:  10,
					Message: "invalid syntax",
				},
			},
		},
		{
			name:    "context_changed with flat fields",
			rawJSON: fmt.Sprintf(`{"seq":5,"ts":"%s","session":"sess-1","type":"context_changed","contexts":["ship_cockpit","supercruise"],"flags":16,"flags2":0,"guiFocus":0}`, now.Format(time.RFC3339Nano)),
			expected: events.Event{
				Seq:     5,
				TS:      now,
				Session: "sess-1",
				Type:    events.TypeContextChanged,
				ContextDetails: &events.ContextDetails{
					Contexts: []string{"ship_cockpit", "supercruise"},
					Flags:    16,
					Flags2:   0,
					GUIFocus: 0,
				},
			},
		},
		{
			name:    "companion_crashed event",
			rawJSON: fmt.Sprintf(`{"seq":10,"ts":"%s","session":"sess-1","type":"companion_crashed","companion":"ship","pid":4096,"exitCode":1,"attempt":2}`, now.Format(time.RFC3339Nano)),
			expected: events.Event{
				Seq:     10,
				TS:      now,
				Session: "sess-1",
				Type:    events.TypeCompanionCrashed,
				CompanionDetails: &events.CompanionDetails{
					Companion: "ship",
					PID:       4096,
					ExitCode:  1,
					Attempt:   2,
				},
			},
		},
		{
			name:    "events_dropped event",
			rawJSON: fmt.Sprintf(`{"seq":99,"ts":"%s","session":"sess-1","type":"events_dropped","count":12}`, now.Format(time.RFC3339Nano)),
			expected: events.Event{
				Seq:     99,
				TS:      now,
				Session: "sess-1",
				Type:    events.TypeEventsDropped,
				Count:   12,
			},
		},
	}

	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			var ev events.Event
			if err := json.Unmarshal([]byte(tc.rawJSON), &ev); err != nil {
				t.Fatalf("Unmarshal failed: %v", err)
			}

			// Clear raw for reflection compare
			ev.Raw = nil
			if !ev.TS.Equal(tc.expected.TS) {
				t.Errorf("TS mismatch: got %v, want %v", ev.TS, tc.expected.TS)
			}
			ev.TS = tc.expected.TS

			if !reflect.DeepEqual(ev, tc.expected) {
				t.Errorf("Event mismatch:\ngot:  %+v\nwant: %+v", ev, tc.expected)
			}

			// Test round-trip Marshal
			marshaled, err := json.Marshal(ev)
			if err != nil {
				t.Fatalf("Marshal failed: %v", err)
			}
			var ev2 events.Event
			if err := json.Unmarshal(marshaled, &ev2); err != nil {
				t.Fatalf("Unmarshal roundtrip failed: %v", err)
			}
			ev2.Raw = nil
			ev2.TS = tc.expected.TS
			if !reflect.DeepEqual(ev2, tc.expected) {
				t.Errorf("Roundtrip mismatch:\ngot:  %+v\nwant: %+v", ev2, tc.expected)
			}
		})
	}
}

func TestTail_SequentialAppends(t *testing.T) {
	tmpDir := t.TempDir()
	logPath := filepath.Join(tmpDir, "events.jsonl")

	f, err := os.Create(logPath)
	if err != nil {
		t.Fatalf("failed to create log file: %v", err)
	}
	defer f.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	evCh, errCh := events.Tail(ctx, logPath, events.WithPollInterval(10*time.Millisecond))

	// Write 3 events one by one
	for i := 1; i <= 3; i++ {
		line := fmt.Sprintf(`{"seq":%d,"ts":"2026-10-15T10:00:0%dZ","session":"sess-1","type":"nav","screen":"scr1","index":%d}`+"\n", i, i, i)
		if _, err := f.WriteString(line); err != nil {
			t.Fatalf("failed to write event: %v", err)
		}
		_ = f.Sync()

		select {
		case ev := <-evCh:
			if ev.Seq != uint64(i) {
				t.Errorf("expected seq %d, got %d", i, ev.Seq)
			}
			if ev.Session != "sess-1" {
				t.Errorf("expected session sess-1, got %s", ev.Session)
			}
		case err := <-errCh:
			t.Fatalf("unexpected tail error: %v", err)
		case <-time.After(2 * time.Second):
			t.Fatalf("timed out waiting for event %d", i)
		}
	}

	// Test partial line write: write partial line, sleep, then write remainder + \n
	part1 := `{"seq":4,"ts":"2026-10-15T10:00:04Z","session":"sess-1",`
	part2 := `"type":"select","screen":"scr1","index":4}` + "\n"

	if _, err := f.WriteString(part1); err != nil {
		t.Fatalf("failed to write part1: %v", err)
	}
	_ = f.Sync()

	time.Sleep(50 * time.Millisecond)

	// Ensure no event arrived prematurely
	select {
	case ev := <-evCh:
		t.Fatalf("received premature event from partial line: %+v", ev)
	default:
	}

	if _, err := f.WriteString(part2); err != nil {
		t.Fatalf("failed to write part2: %v", err)
	}
	_ = f.Sync()

	select {
	case ev := <-evCh:
		if ev.Seq != 4 || ev.Type != events.TypeSelect {
			t.Errorf("expected seq 4 select event, got %+v", ev)
		}
	case err := <-errCh:
		t.Fatalf("unexpected tail error: %v", err)
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for event 4 after completing partial line")
	}
}

func TestTail_TruncationsAndSessionResets(t *testing.T) {
	tmpDir := t.TempDir()
	logPath := filepath.Join(tmpDir, "events.jsonl")

	f, err := os.Create(logPath)
	if err != nil {
		t.Fatalf("failed to create log file: %v", err)
	}
	defer f.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	evCh, errCh := events.Tail(ctx, logPath, events.WithPollInterval(10*time.Millisecond))

	// Write session 1 events
	f.WriteString(`{"seq":1,"ts":"2026-10-15T10:00:01Z","session":"sess-1","type":"plugin_start"}` + "\n")
	f.WriteString(`{"seq":2,"ts":"2026-10-15T10:00:02Z","session":"sess-1","type":"screen_loaded","screen":"nav"}` + "\n")
	_ = f.Sync()

	for i := 1; i <= 2; i++ {
		select {
		case ev := <-evCh:
			if ev.Seq != uint64(i) || ev.Session != "sess-1" {
				t.Fatalf("expected session 1 seq %d, got %+v", i, ev)
			}
		case err := <-errCh:
			t.Fatalf("unexpected error: %v", err)
		case <-time.After(2 * time.Second):
			t.Fatalf("timed out waiting for seq %d", i)
		}
	}

	// Truncate file in place to simulate new session / plugin init
	time.Sleep(30 * time.Millisecond)
	if err := f.Truncate(0); err != nil {
		t.Fatalf("failed to truncate: %v", err)
	}
	if _, err := f.Seek(0, 0); err != nil {
		t.Fatalf("failed to seek: %v", err)
	}

	// Write session 2 events
	f.WriteString(`{"seq":1,"ts":"2026-10-15T11:00:01Z","session":"sess-2","type":"plugin_start"}` + "\n")
	f.WriteString(`{"seq":2,"ts":"2026-10-15T11:00:02Z","session":"sess-2","type":"focus_gained","screen":"nav"}` + "\n")
	_ = f.Sync()

	for i := 1; i <= 2; i++ {
		select {
		case ev := <-evCh:
			if ev.Seq != uint64(i) || ev.Session != "sess-2" {
				t.Fatalf("expected session 2 seq %d, got %+v", i, ev)
			}
		case err := <-errCh:
			t.Fatalf("unexpected error: %v", err)
		case <-time.After(2 * time.Second):
			t.Fatalf("timed out waiting for session 2 seq %d", i)
		}
	}
}

func TestTail_RotationAndRoll(t *testing.T) {
	tmpDir := t.TempDir()
	logPath := filepath.Join(tmpDir, "events.jsonl")
	rolledPath := filepath.Join(tmpDir, "events.1.jsonl")

	f, err := os.Create(logPath)
	if err != nil {
		t.Fatalf("failed to create log: %v", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	evCh, errCh := events.Tail(ctx, logPath, events.WithPollInterval(10*time.Millisecond))

	// Write seq 1 and 2
	f.WriteString(`{"seq":1,"ts":"2026-10-15T10:00:01Z","session":"sess-rot","type":"plugin_start"}` + "\n")
	f.WriteString(`{"seq":2,"ts":"2026-10-15T10:00:02Z","session":"sess-rot","type":"screen_loaded","screen":"power"}` + "\n")
	_ = f.Sync()

	for i := 1; i <= 2; i++ {
		select {
		case ev := <-evCh:
			if ev.Seq != uint64(i) {
				t.Fatalf("expected seq %d, got %+v", i, ev)
			}
		case err := <-errCh:
			t.Fatalf("unexpected error: %v", err)
		case <-time.After(2 * time.Second):
			t.Fatalf("timeout waiting for seq %d", i)
		}
	}

	// Rotate log: close old file, rename to events.1.jsonl, create new events.jsonl
	_ = f.Close()
	if err := os.Rename(logPath, rolledPath); err != nil {
		t.Fatalf("failed to rename for roll: %v", err)
	}

	// Create fresh events.jsonl
	fNew, err := os.Create(logPath)
	if err != nil {
		t.Fatalf("failed to create new file after roll: %v", err)
	}
	defer fNew.Close()

	// Write seq 3 and 4 to fresh events.jsonl
	fNew.WriteString(`{"seq":3,"ts":"2026-10-15T10:00:03Z","session":"sess-rot","type":"select","screen":"power","index":1}` + "\n")
	fNew.WriteString(`{"seq":4,"ts":"2026-10-15T10:00:04Z","session":"sess-rot","type":"adjust","screen":"power","index":1}` + "\n")
	_ = fNew.Sync()

	for i := 3; i <= 4; i++ {
		select {
		case ev := <-evCh:
			if ev.Seq != uint64(i) || ev.Session != "sess-rot" {
				t.Fatalf("expected post-rotation seq %d sess-rot, got %+v", i, ev)
			}
		case err := <-errCh:
			t.Fatalf("unexpected error: %v", err)
		case <-time.After(2 * time.Second):
			t.Fatalf("timeout waiting for post-rotation seq %d", i)
		}
	}
}

func TestTail_StartupReplayRolled(t *testing.T) {
	tmpDir := t.TempDir()
	logPath := filepath.Join(tmpDir, "events.jsonl")
	rolledPath := filepath.Join(tmpDir, "events.1.jsonl")

	// Prepare rolled file with seq 1..2
	if err := os.WriteFile(rolledPath, []byte(
		`{"seq":1,"ts":"2026-10-15T10:00:01Z","session":"sess-startup","type":"plugin_start"}`+"\n"+
			`{"seq":2,"ts":"2026-10-15T10:00:02Z","session":"sess-startup","type":"screen_loaded","screen":"cargo"}`+"\n",
	), 0644); err != nil {
		t.Fatalf("failed to write rolled file: %v", err)
	}

	// Prepare active file with seq 3..4
	if err := os.WriteFile(logPath, []byte(
		`{"seq":3,"ts":"2026-10-15T10:00:03Z","session":"sess-startup","type":"focus_gained","screen":"cargo"}`+"\n"+
			`{"seq":4,"ts":"2026-10-15T10:00:04Z","session":"sess-startup","type":"select","screen":"cargo","index":0}`+"\n",
	), 0644); err != nil {
		t.Fatalf("failed to write live file: %v", err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	evCh, errCh := events.Tail(ctx, logPath, events.WithPollInterval(10*time.Millisecond))

	for i := 1; i <= 4; i++ {
		select {
		case ev := <-evCh:
			if ev.Seq != uint64(i) || ev.Session != "sess-startup" {
				t.Fatalf("expected seq %d sess-startup, got %+v", i, ev)
			}
		case err := <-errCh:
			t.Fatalf("unexpected error: %v", err)
		case <-time.After(2 * time.Second):
			t.Fatalf("timeout waiting for startup replay seq %d", i)
		}
	}
}

func TestTail_ContextCancellation(t *testing.T) {
	tmpDir := t.TempDir()
	logPath := filepath.Join(tmpDir, "events.jsonl")

	if err := os.WriteFile(logPath, []byte(`{"seq":1,"ts":"2026-10-15T10:00:01Z","session":"sess-c","type":"plugin_start"}`+"\n"), 0644); err != nil {
		t.Fatalf("failed to write file: %v", err)
	}

	ctx, cancel := context.WithCancel(context.Background())
	evCh, errCh := events.Tail(ctx, logPath, events.WithPollInterval(10*time.Millisecond))

	// Receive the 1 event
	<-evCh

	// Cancel context
	cancel()

	// Wait for channels to close
	timeout := time.After(2 * time.Second)
	for evCh != nil || errCh != nil {
		select {
		case _, ok := <-evCh:
			if !ok {
				evCh = nil
			}
		case _, ok := <-errCh:
			if !ok {
				errCh = nil
			}
		case <-timeout:
			t.Fatal("channels not closed after context cancellation")
		}
	}
}

func TestTail_MalformedJSONRecovery(t *testing.T) {
	tmpDir := t.TempDir()
	logPath := filepath.Join(tmpDir, "events.jsonl")

	f, err := os.Create(logPath)
	if err != nil {
		t.Fatalf("failed to create file: %v", err)
	}
	defer f.Close()

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()

	evCh, errCh := events.Tail(ctx, logPath, events.WithPollInterval(10*time.Millisecond))

	var (
		errMu   sync.Mutex
		errList []error
	)
	go func() {
		for err := range errCh {
			errMu.Lock()
			errList = append(errList, err)
			errMu.Unlock()
		}
	}()

	// Write bad line followed by valid line
	f.WriteString("this is not json\n")
	f.WriteString(`{"seq":1,"ts":"2026-10-15T10:00:01Z","session":"sess-err","type":"plugin_start"}` + "\n")
	_ = f.Sync()

	select {
	case ev := <-evCh:
		if ev.Seq != 1 || ev.Type != events.TypePluginStart {
			t.Fatalf("expected valid event after bad line, got %+v", ev)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("timed out waiting for event after malformed line")
	}

	errMu.Lock()
	defer errMu.Unlock()
	if len(errList) == 0 {
		t.Fatal("expected at least 1 parse error reported on errCh")
	}
}
