package manifest_test

import (
	"encoding/json"
	"os"
	"path/filepath"
	"testing"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/manifest"
)

func sampleValidDisplay() display.Display {
	return display.Display{
		SchemaVersion: 1,
		Title:         "TEST DISPLAY",
		Tabs: []display.Tab{
			{
				ID:    "tab1",
				Title: "MAIN",
				Type:  "list",
				Items: []display.Item{
					{ID: "item1", Label: "Speed", Value: "100 m/s"},
				},
			},
		},
	}
}

func writeJSONFile(t *testing.T, path string, v any) {
	t.Helper()
	if err := os.MkdirAll(filepath.Dir(path), 0755); err != nil {
		t.Fatalf("failed to create dir: %v", err)
	}
	data, err := json.MarshalIndent(v, "", "  ")
	if err != nil {
		t.Fatalf("failed to marshal json: %v", err)
	}
	if err := os.WriteFile(path, data, 0644); err != nil {
		t.Fatalf("failed to write file: %v", err)
	}
}

func TestManifestValidation_Valid(t *testing.T) {
	pkgDir := filepath.Join(t.TempDir(), "testpkg")
	if err := os.MkdirAll(pkgDir, 0755); err != nil {
		t.Fatal(err)
	}

	screenPath := filepath.Join(pkgDir, "screens", "main.json")
	writeJSONFile(t, screenPath, sampleValidDisplay())

	m := manifest.Manifest{
		ManifestVersion: 1,
		ID:              "testpkg",
		Name:            "Test Package",
		Version:         "1.0.0",
		Author:          "Commander",
		Screens: []manifest.Screen{
			{
				ID:    "main",
				File:  "screens/main.json",
				Title: "Main Screen",
				Pose: &manifest.Pose{
					Position: []float64{-0.45, -0.22, -0.48},
					PitchDeg: -22,
					YawDeg:   32,
					WidthM:   0.24,
					HeightM:  0.16,
				},
				Contexts: []string{"ship_cockpit", "fighter"},
			},
		},
		Companions: []manifest.Companion{
			{
				ID:      "test-comp",
				Exe:     "bin/test.exe",
				Restart: "on-failure",
			},
		},
	}
	writeJSONFile(t, filepath.Join(pkgDir, "manifest.json"), m)

	parsed, errs := manifest.ValidatePackage(pkgDir)
	if len(errs) > 0 {
		t.Fatalf("expected 0 errors, got: %v", errs)
	}
	if parsed == nil || parsed.ID != "testpkg" {
		t.Fatalf("expected parsed manifest id 'testpkg', got %+v", parsed)
	}
}

func TestManifestValidation_InvalidManifestVersion(t *testing.T) {
	pkgDir := filepath.Join(t.TempDir(), "testpkg")
	_ = os.MkdirAll(pkgDir, 0755)

	screenPath := filepath.Join(pkgDir, "screens", "main.json")
	writeJSONFile(t, screenPath, sampleValidDisplay())

	m := manifest.Manifest{
		ManifestVersion: 2, // invalid
		ID:              "testpkg",
		Name:            "Test Package",
		Screens: []manifest.Screen{
			{ID: "main", File: "screens/main.json", Title: "Main"},
		},
	}
	writeJSONFile(t, filepath.Join(pkgDir, "manifest.json"), m)

	_, errs := manifest.ValidatePackage(pkgDir)
	if len(errs) == 0 {
		t.Fatal("expected error for manifestVersion != 1")
	}
}

func TestManifestValidation_IDMismatch(t *testing.T) {
	pkgDir := filepath.Join(t.TempDir(), "expected-name")
	_ = os.MkdirAll(pkgDir, 0755)

	screenPath := filepath.Join(pkgDir, "screens", "main.json")
	writeJSONFile(t, screenPath, sampleValidDisplay())

	m := manifest.Manifest{
		ManifestVersion: 1,
		ID:              "different-name",
		Name:            "Test Package",
		Screens: []manifest.Screen{
			{ID: "main", File: "screens/main.json", Title: "Main"},
		},
	}
	writeJSONFile(t, filepath.Join(pkgDir, "manifest.json"), m)

	_, errs := manifest.ValidatePackage(pkgDir)
	if len(errs) == 0 {
		t.Fatal("expected error for directory name mismatch")
	}
}

func TestManifestValidation_UnsafePath(t *testing.T) {
	pkgDir := filepath.Join(t.TempDir(), "safepkg")
	_ = os.MkdirAll(pkgDir, 0755)

	m := manifest.Manifest{
		ManifestVersion: 1,
		ID:              "safepkg",
		Name:            "Test Package",
		Screens: []manifest.Screen{
			{ID: "escape", File: "../escape.json", Title: "Escape"},
		},
		Companions: []manifest.Companion{
			{ID: "badexe", Exe: "../bin/bad.exe"},
		},
	}
	writeJSONFile(t, filepath.Join(pkgDir, "manifest.json"), m)

	_, errs := manifest.ValidatePackage(pkgDir)
	if len(errs) < 2 {
		t.Fatalf("expected at least 2 path safety errors, got: %v", errs)
	}
}

func TestManifestValidation_InvalidScreenDisplay(t *testing.T) {
	pkgDir := filepath.Join(t.TempDir(), "badscreen")
	_ = os.MkdirAll(pkgDir, 0755)

	// Invalid display: schemaVersion 99
	badDisp := sampleValidDisplay()
	badDisp.SchemaVersion = 99
	writeJSONFile(t, filepath.Join(pkgDir, "screens", "bad.json"), badDisp)

	m := manifest.Manifest{
		ManifestVersion: 1,
		ID:              "badscreen",
		Name:            "Bad Screen Package",
		Screens: []manifest.Screen{
			{ID: "bad", File: "screens/bad.json", Title: "Bad"},
		},
	}
	writeJSONFile(t, filepath.Join(pkgDir, "manifest.json"), m)

	_, errs := manifest.ValidatePackage(pkgDir)
	if len(errs) == 0 {
		t.Fatal("expected error for invalid display JSON")
	}
}

func TestManifestValidation_InvalidContext(t *testing.T) {
	pkgDir := filepath.Join(t.TempDir(), "badcontext")
	_ = os.MkdirAll(pkgDir, 0755)

	writeJSONFile(t, filepath.Join(pkgDir, "screens", "main.json"), sampleValidDisplay())

	m := manifest.Manifest{
		ManifestVersion: 1,
		ID:              "badcontext",
		Name:            "Bad Context",
		Screens: []manifest.Screen{
			{
				ID:       "main",
				File:     "screens/main.json",
				Title:    "Main",
				Contexts: []string{"ship_cockpit", "hyperspace_dance_party"},
			},
		},
	}
	writeJSONFile(t, filepath.Join(pkgDir, "manifest.json"), m)

	_, errs := manifest.ValidatePackage(pkgDir)
	if len(errs) == 0 {
		t.Fatal("expected error for invalid context")
	}
}

func TestManifestValidation_RenderSizeOverride(t *testing.T) {
	cases := []struct {
		name          string
		width, height int
		wantErr       bool
	}{
		{"both", 512, 128, false},
		{"neither", 0, 0, false},
		{"width only", 512, 0, true},
		{"negative", -1, 128, true},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			pkgDir := filepath.Join(t.TempDir(), "rsize")
			_ = os.MkdirAll(pkgDir, 0755)
			writeJSONFile(t, filepath.Join(pkgDir, "screens", "main.json"), sampleValidDisplay())

			m := manifest.Manifest{
				ManifestVersion: 1,
				ID:              "rsize",
				Name:            "Render Size",
				Screens: []manifest.Screen{
					{
						ID:    "main",
						File:  "screens/main.json",
						Title: "Main",
						Pose: &manifest.Pose{
							Position:     []float64{0, -0.2, -0.5},
							WidthM:       0.24,
							HeightM:      0.06,
							RenderWidth:  tc.width,
							RenderHeight: tc.height,
						},
					},
				},
			}
			writeJSONFile(t, filepath.Join(pkgDir, "manifest.json"), m)

			_, errs := manifest.ValidatePackage(pkgDir)
			if tc.wantErr && len(errs) == 0 {
				t.Fatal("expected render size validation error")
			}
			if !tc.wantErr && len(errs) > 0 {
				t.Fatalf("expected no errors, got: %v", errs)
			}
		})
	}
}
