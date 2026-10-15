package writer_test

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"sync/atomic"
	"testing"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/writer"
)

func sampleDisplay(subtitle string) *display.Display {
	return &display.Display{
		SchemaVersion: display.CurrentSchemaVersion,
		Title:         "MODULES",
		Subtitle:      subtitle,
		StatusBadge: &display.StatusBadge{
			Text:  "ONLINE",
			Color: display.ColorOK,
		},
		FooterHint: "SELECT: DETAILS",
		Tabs: []display.Tab{
			{
				ID:    "integrity",
				Title: "INTEGRITY",
				Type:  display.TabTypeList,
				Items: []display.Item{
					{
						ID:         "slot_fsd",
						Label:      "Frame Shift Drive",
						Sublabel:   "5A",
						Value:      "100%",
						ValueColor: display.ColorOK,
						Checks: []display.Check{
							{Label: "S", State: display.CheckStateDone},
						},
						Action: display.ActionEvent,
					},
				},
			},
		},
	}
}

func TestWriteScreen_Validation(t *testing.T) {
	tempDir := t.TempDir()
	validPath := filepath.Join(tempDir, "screen.json")

	// Nil display
	wrote, err := writer.WriteScreen(validPath, nil)
	if err == nil || wrote {
		t.Fatalf("expected error for nil display, got wrote=%v, err=%v", wrote, err)
	}

	// Empty path
	wrote, err = writer.WriteScreen("", sampleDisplay("TEST"))
	if err == nil || wrote {
		t.Fatalf("expected error for empty destPath, got wrote=%v, err=%v", wrote, err)
	}

	// Invalid display model (e.g. wrong schemaVersion)
	invalidD := sampleDisplay("TEST")
	invalidD.SchemaVersion = 99
	wrote, err = writer.WriteScreen(validPath, invalidD)
	if err == nil || wrote {
		t.Fatalf("expected validation error for invalid schemaVersion, got wrote=%v, err=%v", wrote, err)
	}

	// Verify no file was created on error
	if _, statErr := os.Stat(validPath); !os.IsNotExist(statErr) {
		t.Fatalf("expected no file created on validation failure, statErr=%v", statErr)
	}
}

func TestWriteScreen_AtomicReplacement(t *testing.T) {
	tempDir := t.TempDir()
	screenPath := filepath.Join(tempDir, "subfolder", "screen.json")

	// 1. Initial write
	d1 := sampleDisplay("V1")
	wrote, err := writer.WriteScreen(screenPath, d1)
	if err != nil {
		t.Fatalf("WriteScreen initial write failed: %v", err)
	}
	if !wrote {
		t.Fatalf("expected wrote=true for initial write")
	}

	// Verify file exists and content matches
	data, err := os.ReadFile(screenPath)
	if err != nil {
		t.Fatalf("failed to read written file: %v", err)
	}
	var readD1 display.Display
	if err := json.Unmarshal(data, &readD1); err != nil {
		t.Fatalf("failed to unmarshal written JSON: %v", err)
	}
	if readD1.Subtitle != "V1" {
		t.Fatalf("expected Subtitle='V1', got %q", readD1.Subtitle)
	}

	// Verify no stray temp files in the directory
	entries, err := os.ReadDir(filepath.Dir(screenPath))
	if err != nil {
		t.Fatalf("failed to read dir: %v", err)
	}
	if len(entries) != 1 || entries[0].Name() != "screen.json" {
		t.Fatalf("found unexpected files in dir: %+v", entries)
	}

	// 2. Overwrite atomically with V2
	d2 := sampleDisplay("V2")
	wrote, err = writer.WriteScreen(screenPath, d2)
	if err != nil {
		t.Fatalf("WriteScreen overwrite failed: %v", err)
	}
	if !wrote {
		t.Fatalf("expected wrote=true for updated write")
	}

	data, err = os.ReadFile(screenPath)
	if err != nil {
		t.Fatalf("failed to read updated file: %v", err)
	}
	var readD2 display.Display
	if err := json.Unmarshal(data, &readD2); err != nil {
		t.Fatalf("failed to unmarshal updated JSON: %v", err)
	}
	if readD2.Subtitle != "V2" {
		t.Fatalf("expected Subtitle='V2', got %q", readD2.Subtitle)
	}
}

func TestWriteScreen_ByteComparison(t *testing.T) {
	tempDir := t.TempDir()
	screenPath := filepath.Join(tempDir, "screen.json")

	d := sampleDisplay("IDENTICAL")

	// First write: should write
	wrote, err := writer.WriteScreen(screenPath, d)
	if err != nil || !wrote {
		t.Fatalf("initial write failed: wrote=%v, err=%v", wrote, err)
	}

	info1, err := os.Stat(screenPath)
	if err != nil {
		t.Fatalf("stat failed: %v", err)
	}

	// Wait briefly to ensure mtime would differ if written
	time.Sleep(20 * time.Millisecond)

	// Second write: identical content, should skip write and return (false, nil)
	wrote, err = writer.WriteScreen(screenPath, d)
	if err != nil {
		t.Fatalf("second write failed: %v", err)
	}
	if wrote {
		t.Fatalf("expected wrote=false for identical content, got true")
	}

	info2, err := os.Stat(screenPath)
	if err != nil {
		t.Fatalf("stat 2 failed: %v", err)
	}
	if !info1.ModTime().Equal(info2.ModTime()) {
		t.Fatalf("file was touched when content was identical! mtime1=%v, mtime2=%v", info1.ModTime(), info2.ModTime())
	}

	// Third write: changed content
	dModified := sampleDisplay("CHANGED")
	wrote, err = writer.WriteScreen(screenPath, dModified)
	if err != nil || !wrote {
		t.Fatalf("modified write failed: wrote=%v, err=%v", wrote, err)
	}

	info3, err := os.Stat(screenPath)
	if err != nil {
		t.Fatalf("stat 3 failed: %v", err)
	}
	if info3.ModTime().Equal(info1.ModTime()) {
		t.Logf("note: mtime equal due to timer resolution, but content must change")
	}

	data, err := os.ReadFile(screenPath)
	if err != nil {
		t.Fatalf("read failed: %v", err)
	}
	var readD display.Display
	if err := json.Unmarshal(data, &readD); err != nil {
		t.Fatalf("unmarshal failed: %v", err)
	}
	if readD.Subtitle != "CHANGED" {
		t.Fatalf("expected Subtitle='CHANGED', got %q", readD.Subtitle)
	}
}

func TestCoalescedWriter_BurstWrites(t *testing.T) {
	tempDir := t.TempDir()
	screenPath := filepath.Join(tempDir, "burst_screen.json")

	var writeCount int32
	hook := func(destPath string, wrote bool, err error) {
		if wrote {
			atomic.AddInt32(&writeCount, 1)
		}
	}

	// Throttled at 50ms interval
	cw := writer.NewCoalescedWriter(
		writer.WithInterval(50*time.Millisecond),
		writer.WithWriteHook(hook),
	)

	// Fire 50 rapid updates
	const bursts = 50
	for i := 0; i < bursts; i++ {
		d := sampleDisplay(fmt.Sprintf("burst-%d", i))
		if err := cw.Queue(screenPath, d); err != nil {
			t.Fatalf("Queue failed on iteration %d: %v", i, err)
		}
	}

	// Flush and close
	if err := cw.Close(); err != nil {
		t.Fatalf("Close failed: %v", err)
	}

	totalWrites := atomic.LoadInt32(&writeCount)
	t.Logf("50 burst updates resulted in %d disk writes", totalWrites)

	// 50 fast writes must coalesce to <= few disk writes (typically 1 or 2)
	if totalWrites > 5 {
		t.Fatalf("expected <= 5 disk writes for burst of 50 updates, got %d", totalWrites)
	}

	// Final state on disk MUST be burst-49
	data, err := os.ReadFile(screenPath)
	if err != nil {
		t.Fatalf("read failed: %v", err)
	}
	var finalD display.Display
	if err := json.Unmarshal(data, &finalD); err != nil {
		t.Fatalf("unmarshal failed: %v", err)
	}
	if finalD.Subtitle != "burst-49" {
		t.Fatalf("expected final subtitle 'burst-49', got %q", finalD.Subtitle)
	}
}

func TestCoalescedWriter_FlushImmediate(t *testing.T) {
	tempDir := t.TempDir()
	screenPath := filepath.Join(tempDir, "flush_screen.json")

	cw := writer.NewCoalescedWriter(
		writer.WithInterval(200 * time.Millisecond),
	)
	defer cw.Close()

	d := sampleDisplay("FLUSH_TEST")
	if err := cw.Queue(screenPath, d); err != nil {
		t.Fatalf("Queue failed: %v", err)
	}

	// Flush immediately blocks until written
	start := time.Now()
	if err := cw.Flush(); err != nil {
		t.Fatalf("Flush failed: %v", err)
	}
	elapsed := time.Since(start)

	// Should not have waited the full 200ms
	t.Logf("Flush took %v", elapsed)

	data, err := os.ReadFile(screenPath)
	if err != nil {
		t.Fatalf("read failed: %v", err)
	}
	var readD display.Display
	if err := json.Unmarshal(data, &readD); err != nil {
		t.Fatalf("unmarshal failed: %v", err)
	}
	if readD.Subtitle != "FLUSH_TEST" {
		t.Fatalf("expected 'FLUSH_TEST', got %q", readD.Subtitle)
	}
}

func TestCoalescedWriter_MultipleScreens(t *testing.T) {
	tempDir := t.TempDir()
	screen1 := filepath.Join(tempDir, "screen1.json")
	screen2 := filepath.Join(tempDir, "screen2.json")

	cw := writer.NewCoalescedWriter(
		writer.WithInterval(30 * time.Millisecond),
	)

	var wg sync.WaitGroup
	wg.Add(2)

	go func() {
		defer wg.Done()
		for i := 0; i < 20; i++ {
			_ = cw.Queue(screen1, sampleDisplay(fmt.Sprintf("s1-%d", i)))
			time.Sleep(2 * time.Millisecond)
		}
	}()

	go func() {
		defer wg.Done()
		for i := 0; i < 20; i++ {
			_ = cw.Queue(screen2, sampleDisplay(fmt.Sprintf("s2-%d", i)))
			time.Sleep(2 * time.Millisecond)
		}
	}()

	wg.Wait()
	if err := cw.Close(); err != nil {
		t.Fatalf("Close failed: %v", err)
	}

	// Verify both screens exist and have final values
	data1, err := os.ReadFile(screen1)
	if err != nil {
		t.Fatalf("read screen1 failed: %v", err)
	}
	var d1 display.Display
	_ = json.Unmarshal(data1, &d1)
	if d1.Subtitle != "s1-19" {
		t.Fatalf("expected s1-19, got %q", d1.Subtitle)
	}

	data2, err := os.ReadFile(screen2)
	if err != nil {
		t.Fatalf("read screen2 failed: %v", err)
	}
	var d2 display.Display
	_ = json.Unmarshal(data2, &d2)
	if d2.Subtitle != "s2-19" {
		t.Fatalf("expected s2-19, got %q", d2.Subtitle)
	}
}

func TestCoalescedWriter_ClosedBehavior(t *testing.T) {
	cw := writer.NewCoalescedWriter()
	if err := cw.Close(); err != nil {
		t.Fatalf("initial close failed: %v", err)
	}

	// Idempotent close
	if err := cw.Close(); err != nil {
		t.Fatalf("second close failed: %v", err)
	}

	// Queue after close must error
	err := cw.Queue("test.json", sampleDisplay("CLOSED"))
	if err == nil {
		t.Fatalf("expected error queueing after close, got nil")
	}
}
