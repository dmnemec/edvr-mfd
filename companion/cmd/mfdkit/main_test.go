package main

import (
	"bytes"
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/manifest"
)

func createTestPackage(t *testing.T, dirName string, invalidScreen bool) string {
	t.Helper()
	pkgDir := filepath.Join(t.TempDir(), dirName)
	if err := os.MkdirAll(pkgDir, 0755); err != nil {
		t.Fatal(err)
	}

	screenDir := filepath.Join(pkgDir, "screens")
	if err := os.MkdirAll(screenDir, 0755); err != nil {
		t.Fatal(err)
	}

	disp := display.Display{
		SchemaVersion: 1,
		Title:         "TEST DISPLAY",
		Tabs: []display.Tab{
			{
				ID:    "tab1",
				Title: "MAIN",
				Type:  "list",
				Items: []display.Item{
					{ID: "i1", Label: "Integrity", Value: "100%"},
				},
			},
		},
	}
	if invalidScreen {
		disp.SchemaVersion = 42 // invalid schema version
	}

	dispBytes, _ := json.MarshalIndent(disp, "", "  ")
	_ = os.WriteFile(filepath.Join(screenDir, "main.json"), dispBytes, 0644)

	m := manifest.Manifest{
		ManifestVersion: 1,
		ID:              dirName,
		Name:            "Test Package",
		Screens: []manifest.Screen{
			{
				ID:       "main",
				File:     "screens/main.json",
				Title:    "Main Screen",
				Contexts: []string{"ship_cockpit"},
			},
		},
		Companions: []manifest.Companion{
			{
				ID:      "test-agent",
				Exe:     "bin/agent.exe",
				Restart: "on-failure",
			},
		},
	}
	mBytes, _ := json.MarshalIndent(m, "", "  ")
	_ = os.WriteFile(filepath.Join(pkgDir, "manifest.json"), mBytes, 0644)

	return pkgDir
}

func TestCLI_HelpAndUnknown(t *testing.T) {
	var stdout, stderr bytes.Buffer

	// Help command
	code := run([]string{"help"}, &stdout, &stderr)
	if code != 0 {
		t.Errorf("expected exit code 0 for help, got %d", code)
	}
	if !strings.Contains(stdout.String(), "mfdkit - Elite Dangerous VR MFD") {
		t.Errorf("unexpected help output: %s", stdout.String())
	}

	// Unknown command
	stdout.Reset()
	stderr.Reset()
	code = run([]string{"nonexistent"}, &stdout, &stderr)
	if code != 1 {
		t.Errorf("expected exit code 1 for unknown command, got %d", code)
	}
	if !strings.Contains(stderr.String(), "unknown command: nonexistent") {
		t.Errorf("unexpected unknown command error: %s", stderr.String())
	}
}

func TestCLI_Validate_Success(t *testing.T) {
	pkgDir := createTestPackage(t, "mypackage", false)

	var stdout, stderr bytes.Buffer
	code := run([]string{"validate", pkgDir}, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("expected exit code 0, got %d. stderr: %s", code, stderr.String())
	}
	if !strings.Contains(stdout.String(), "validated successfully") {
		t.Errorf("expected success message, got: %s", stdout.String())
	}
}

func TestCLI_Validate_Failure(t *testing.T) {
	pkgDir := createTestPackage(t, "badpackage", true)

	var stdout, stderr bytes.Buffer
	code := run([]string{"validate", pkgDir}, &stdout, &stderr)
	if code == 0 {
		t.Fatalf("expected non-zero exit code for invalid package, got 0")
	}
	if !strings.Contains(stderr.String(), "Package validation FAILED") {
		t.Errorf("expected failure message, got: %s", stderr.String())
	}
}

func TestCLI_Validate_MissingDir(t *testing.T) {
	var stdout, stderr bytes.Buffer
	code := run([]string{"validate"}, &stdout, &stderr)
	if code == 0 {
		t.Fatalf("expected non-zero exit code for missing package dir")
	}
	if !strings.Contains(stderr.String(), "package directory argument is required") {
		t.Errorf("expected missing dir message, got: %s", stderr.String())
	}
}

func TestCLI_RecordAndReplay(t *testing.T) {
	journalDir := filepath.Join(t.TempDir(), "live_journal")
	sessionDir := filepath.Join(t.TempDir(), "recorded_session")
	destDir := filepath.Join(t.TempDir(), "replayed_output")

	_ = os.MkdirAll(journalDir, 0755)

	// Create initial journal file
	jFile := filepath.Join(journalDir, "Journal.2026-10-15T100000.01.log")
	_ = os.WriteFile(jFile, []byte(`{"timestamp":"2026-10-15T10:00:00Z","event":"Commander","Name":"TestPilot"}`+"\n"), 0644)
	_ = os.WriteFile(filepath.Join(journalDir, "Status.json"), []byte(`{"event":"Status","Flags":0}`), 0644)

	// Test record with --duration
	var stdout, stderr bytes.Buffer
	recordArgs := []string{
		"record",
		"--journal-dir", journalDir,
		"--out", sessionDir,
		"--commander", "TestPilot",
		"--duration", "100ms",
	}

	code := run(recordArgs, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("record exited with %d: %s", code, stderr.String())
	}
	if !strings.Contains(stdout.String(), "Recording stopped") {
		t.Errorf("unexpected record stdout: %s", stdout.String())
	}

	// Verify session.json exists
	sessionPath := filepath.Join(sessionDir, "session.json")
	if _, err := os.Stat(sessionPath); err != nil {
		t.Fatalf("session.json missing after record: %v", err)
	}

	// Test replay with instant speed
	stdout.Reset()
	stderr.Reset()
	replayArgs := []string{
		"replay",
		"--in", sessionDir,
		"--out", destDir,
		"--speed", "instant",
	}

	code = run(replayArgs, &stdout, &stderr)
	if code != 0 {
		t.Fatalf("replay exited with %d: %s", code, stderr.String())
	}
	if !strings.Contains(stdout.String(), "Replay completed successfully") {
		t.Errorf("unexpected replay stdout: %s", stdout.String())
	}

	// Verify destination received files
	replayedStatus := filepath.Join(destDir, "Status.json")
	if _, err := os.Stat(replayedStatus); err != nil {
		t.Fatalf("destination missing Status.json: %v", err)
	}
}

func TestCLI_Replay_MissingFlags(t *testing.T) {
	var stdout, stderr bytes.Buffer

	// Missing --out
	code := run([]string{"replay", "--in", "some_dir"}, &stdout, &stderr)
	if code == 0 {
		t.Errorf("expected failure when --out is missing")
	}

	// Missing --in
	stdout.Reset()
	stderr.Reset()
	code = run([]string{"replay", "--out", "some_dir"}, &stdout, &stderr)
	if code == 0 {
		t.Errorf("expected failure when --in is missing")
	}

	// Invalid speed
	stdout.Reset()
	stderr.Reset()
	code = run([]string{"replay", "--in", "dir1", "--out", "dir2", "--speed", "ludicrous"}, &stdout, &stderr)
	if code == 0 {
		t.Errorf("expected failure for invalid speed")
	}
}
