package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/events"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/writer"
)

func TestStarClassIcon(t *testing.T) {
	tests := []struct {
		starClass string
		expected  string
	}{
		{"N", display.IconNeutron},
		{"N 1", display.IconNeutron},
		{"Neutron", display.IconNeutron},
		{"D", display.IconWhiteDwarf},
		{"DA", display.IconWhiteDwarf},
		{"DAB", display.IconWhiteDwarf},
		{"DAV", display.IconWhiteDwarf},
		{"DC", display.IconWhiteDwarf},
		{"K", display.IconFuel},
		{"G", display.IconFuel},
		{"B", display.IconFuel},
		{"F", display.IconFuel},
		{"O", display.IconFuel},
		{"A", display.IconFuel},
		{"M", display.IconFuel},
		{"K2 V", display.IconFuel},
		{"G5", display.IconFuel},
		{"m", display.IconFuel}, // case-insensitive
		{"T", ""},
		{"TTS", ""},
		{"Black Hole", ""},
		{"", ""},
	}

	for _, tc := range tests {
		got := StarClassIcon(tc.starClass)
		if got != tc.expected {
			t.Errorf("StarClassIcon(%q) = %q; expected %q", tc.starClass, got, tc.expected)
		}
	}
}

func sampleNavRouteSnapshot() elite.NavRouteSnapshot {
	return elite.NavRouteSnapshot{
		Timestamp: time.Now(),
		Event:     "NavRoute",
		Route: []elite.RouteEntry{
			{
				StarSystem:    "Sol",
				SystemAddress: 104183999512,
				StarPos:       [3]float64{0.0, 0.0, 0.0},
				StarClass:     "G",
			},
			{
				StarSystem:    "Alpha Centauri",
				SystemAddress: 104183999513,
				StarPos:       [3]float64{3.0, 1.0, 2.0},
				StarClass:     "M",
			},
			{
				StarSystem:    "Jackson's Lighthouse",
				SystemAddress: 104183999514,
				StarPos:       [3]float64{100.0, 50.0, 30.0},
				StarClass:     "N",
			},
			{
				StarSystem:    "Sirius B",
				SystemAddress: 104183999515,
				StarPos:       [3]float64{120.0, 60.0, 40.0},
				StarClass:     "DA",
			},
		},
	}
}

func TestNavRouteTab_RenderingAndProgression(t *testing.T) {
	mockCb := &MockClipboard{}
	engine := NewRouteEngine("", WithClipboard(mockCb))

	// Initial empty state
	dInit := engine.BuildDisplay()
	if err := display.Validate(dInit); err != nil {
		t.Fatalf("Validation error on initial display: %v", err)
	}
	if dInit.Tabs[0].Type != display.TabTypeText {
		t.Errorf("Expected initial NAVROUTE tab to be text, got %s", dInit.Tabs[0].Type)
	}

	// Update with sample NavRoute
	navSnap := sampleNavRouteSnapshot()
	engine.HandleNavRoute(navSnap)
	engine.HandleLocation("Sol")

	d1 := engine.BuildDisplay()
	if err := display.Validate(d1); err != nil {
		t.Fatalf("Validation error on plotted route: %v", err)
	}

	tab0 := d1.Tabs[0]
	if tab0.Type != display.TabTypeList {
		t.Fatalf("Expected NAVROUTE tab to be list, got %s", tab0.Type)
	}
	if len(tab0.Items) != 4 {
		t.Fatalf("Expected 4 items, got %d", len(tab0.Items))
	}

	// Sol should have HERE badge
	if tab0.Items[0].Badge != "HERE" {
		t.Errorf("Expected Sol to have HERE badge, got %q", tab0.Items[0].Badge)
	}
	if tab0.Items[0].Icon != display.IconFuel {
		t.Errorf("Expected Sol icon %q, got %q", display.IconFuel, tab0.Items[0].Icon)
	}
	if tab0.Items[0].Action != display.ActionClipboard {
		t.Errorf("Expected action clipboard, got %s", tab0.Items[0].Action)
	}

	// Check Jackson's Lighthouse icon
	if tab0.Items[2].Icon != display.IconNeutron {
		t.Errorf("Expected Jackson's Lighthouse icon %q, got %q", display.IconNeutron, tab0.Items[2].Icon)
	}

	// Check Sirius B icon
	if tab0.Items[3].Icon != display.IconWhiteDwarf {
		t.Errorf("Expected Sirius B icon %q, got %q", display.IconWhiteDwarf, tab0.Items[3].Icon)
	}

	// Header checks
	if !strings.Contains(d1.Subtitle, "3 JUMPS") {
		t.Errorf("Expected Subtitle to mention 3 JUMPS, got %q", d1.Subtitle)
	}
	if !strings.Contains(d1.Subtitle, "NEXT: Alpha Centauri") {
		t.Errorf("Expected Subtitle to mention NEXT: Alpha Centauri, got %q", d1.Subtitle)
	}
	if d1.StatusBadge == nil || d1.StatusBadge.Text != "3 REMAINING" {
		t.Errorf("Expected status badge '3 REMAINING', got %+v", d1.StatusBadge)
	}

	// Jump to Alpha Centauri
	engine.HandleFSDJump("Alpha Centauri")
	d2 := engine.BuildDisplay()

	if d2.Tabs[0].Items[0].ValueColor != display.ColorDim {
		t.Errorf("Expected Sol to be dimmed, got %s", d2.Tabs[0].Items[0].ValueColor)
	}
	if d2.Tabs[0].Items[1].Badge != "HERE" {
		t.Errorf("Expected Alpha Centauri to have HERE badge, got %q", d2.Tabs[0].Items[1].Badge)
	}
	if !strings.Contains(d2.Subtitle, "2 JUMPS") {
		t.Errorf("Expected Subtitle to mention 2 JUMPS, got %q", d2.Subtitle)
	}

	// Jump to Destination Sirius B
	engine.HandleFSDJump("Sirius B")
	d3 := engine.BuildDisplay()
	if d3.StatusBadge.Text != "ARRIVED" {
		t.Errorf("Expected status badge 'ARRIVED', got %q", d3.StatusBadge.Text)
	}
	if d3.Tabs[0].Items[3].Badge != "HERE" {
		t.Errorf("Expected Sirius B to have HERE badge")
	}

	// Clear NavRoute
	engine.HandleNavRouteClear()
	dCleared := engine.BuildDisplay()
	if dCleared.Tabs[0].Type != display.TabTypeText {
		t.Errorf("Expected NAVROUTE tab to revert to text upon NavRouteClear")
	}
}

const sampleSpanshCSV = `System Name, Distance To Arrival, Distance Remaining, Neutron Star, Jumps
Sol, 0.00, 22000.50, No, 0
Col 285 Sector XY-Z d1-2, 178.50, 21822.00, Yes, 1
Prua Phoe TS-U d2-10, 245.20, 21576.80, Yes, 1
Colonia, 150.00, 0.00, No, 1
`

func TestSpanshCSV_Parsing(t *testing.T) {
	wps, err := ParseSpanshCSV(strings.NewReader(sampleSpanshCSV))
	if err != nil {
		t.Fatalf("ParseSpanshCSV failed: %v", err)
	}

	if len(wps) != 4 {
		t.Fatalf("Expected 4 waypoints, got %d", len(wps))
	}

	if wps[0].SystemName != "Sol" || wps[0].NeutronStar {
		t.Errorf("Unexpected waypoint 0: %+v", wps[0])
	}
	if wps[1].SystemName != "Col 285 Sector XY-Z d1-2" || !wps[1].NeutronStar {
		t.Errorf("Unexpected waypoint 1: %+v", wps[1])
	}
	if wps[1].DistanceToArrival != 178.50 || wps[1].DistanceRemaining != 21822.00 {
		t.Errorf("Unexpected distances for waypoint 1: %+v", wps[1])
	}
}

func TestSpanshTab_ProgressionAndAutoCopy(t *testing.T) {
	tempDir := t.TempDir()
	csvPath := filepath.Join(tempDir, "route.csv")
	iniPath := filepath.Join(tempDir, "route.ini")

	if err := os.WriteFile(csvPath, []byte(sampleSpanshCSV), 0644); err != nil {
		t.Fatalf("Failed to write mock route.csv: %v", err)
	}

	mockCb := &MockClipboard{}
	engine := NewRouteEngine(
		csvPath,
		WithClipboard(mockCb),
		WithIniPath(iniPath),
	)

	// Turn on auto-copy
	engine.ToggleAutoCopy()
	if !engine.autoCopy {
		t.Fatalf("Expected autoCopy to be true")
	}

	d1 := engine.BuildDisplay()
	if err := display.Validate(d1); err != nil {
		t.Fatalf("Validation error on Spansh display: %v", err)
	}

	tabSpansh := d1.Tabs[1]
	if tabSpansh.Type != display.TabTypeList {
		t.Fatalf("Expected Spansh tab to be list, got %s", tabSpansh.Type)
	}
	// Item 0 is auto_copy toggle, followed by 4 waypoints = 5 items
	if len(tabSpansh.Items) != 5 {
		t.Fatalf("Expected 5 items in Spansh tab, got %d", len(tabSpansh.Items))
	}
	if tabSpansh.Items[0].ID != "auto_copy" || tabSpansh.Items[0].Value != "ON" {
		t.Errorf("Unexpected auto_copy item: %+v", tabSpansh.Items[0])
	}

	// Arrive at Col 285 Sector XY-Z d1-2 via FSDJump
	engine.HandleFSDJump("Col 285 Sector XY-Z d1-2")

	// Next waypoint is Prua Phoe TS-U d2-10, should be auto-copied
	expectedCopied := "Prua Phoe TS-U d2-10"
	if mockCb.LastCopied != expectedCopied {
		t.Errorf("Expected auto-copied system %q, got %q", expectedCopied, mockCb.LastCopied)
	}

	d2 := engine.BuildDisplay()
	spanshItems := d2.Tabs[1].Items
	// Sol (index 1) should be dim
	if spanshItems[1].ValueColor != display.ColorDim {
		t.Errorf("Expected Sol to be dim in Spansh tab, got %s", spanshItems[1].ValueColor)
	}
	// Col 285 (index 2) should have HERE badge
	if spanshItems[2].Badge != "HERE" {
		t.Errorf("Expected Col 285 to have HERE badge, got %q", spanshItems[2].Badge)
	}

	// Next jump to Prua Phoe TS-U d2-10
	engine.HandleFSDJump("Prua Phoe TS-U d2-10")
	if mockCb.LastCopied != "Colonia" {
		t.Errorf("Expected auto-copied system 'Colonia', got %q", mockCb.LastCopied)
	}
}

func TestEventHandling_AutoCopyAndSelect(t *testing.T) {
	tempDir := t.TempDir()
	csvPath := filepath.Join(tempDir, "route.csv")
	iniPath := filepath.Join(tempDir, "route.ini")

	if err := os.WriteFile(csvPath, []byte(sampleSpanshCSV), 0644); err != nil {
		t.Fatalf("Failed to write mock route.csv: %v", err)
	}

	mockCb := &MockClipboard{}
	engine := NewRouteEngine(
		csvPath,
		WithClipboard(mockCb),
		WithIniPath(iniPath),
	)

	if engine.autoCopy != false {
		t.Fatalf("Expected initial autoCopy to be false")
	}

	// 1. Select event on auto_copy toggles it ON
	engine.HandleEvent(events.Event{
		Type:   events.TypeSelect,
		ItemID: "auto_copy",
	})
	if !engine.autoCopy {
		t.Errorf("Expected autoCopy to be toggled to true by select event")
	}

	// Check route.ini saved state
	savedCfg := LoadRouteINI(iniPath)
	if !savedCfg.AutoCopy {
		t.Errorf("Expected route.ini to have auto_copy=true")
	}

	// 2. Adjust event toggles it back OFF
	engine.HandleEvent(events.Event{
		Type:   events.TypeAdjust,
		ItemID: "auto_copy",
	})
	if engine.autoCopy {
		t.Errorf("Expected autoCopy to be toggled to false by adjust event")
	}

	// 3. Select on waypoint copies system name
	engine.HandleEvent(events.Event{
		Type:   events.TypeSelect,
		Action: display.ActionClipboard,
		Label:  "Colonia",
	})
	if mockCb.LastCopied != "Colonia" {
		t.Errorf("Expected clipboard to receive 'Colonia', got %q", mockCb.LastCopied)
	}
}

func TestCoalescedScreenWriter_Output(t *testing.T) {
	tempDir := t.TempDir()
	destScreen := filepath.Join(tempDir, "screens", "route.json")

	w := writer.NewCoalescedWriter(writer.WithInterval(10 * time.Millisecond))
	defer w.Close()

	engine := NewRouteEngine("", WithWriter(w, destScreen))
	engine.HandleNavRoute(sampleNavRouteSnapshot())
	engine.HandleLocation("Sol")

	// Flush to ensure disk write
	if err := w.Flush(); err != nil {
		t.Fatalf("Flush failed: %v", err)
	}

	// Verify file was written
	data, err := os.ReadFile(destScreen)
	if err != nil {
		t.Fatalf("Failed to read generated route.json: %v", err)
	}

	var d display.Display
	if err := json.Unmarshal(data, &d); err != nil {
		t.Fatalf("Generated route.json failed JSON unmarshal: %v", err)
	}

	if err := display.Validate(&d); err != nil {
		t.Fatalf("Generated route.json failed Schema v1 validation: %v", err)
	}

	if d.Title != "ROUTE" {
		t.Errorf("Expected Title 'ROUTE', got %q", d.Title)
	}
	if len(d.Tabs) != 2 {
		t.Fatalf("Expected 2 tabs, got %d", len(d.Tabs))
	}
	if d.Tabs[0].ID != "navroute" || d.Tabs[1].ID != "spansh" {
		t.Errorf("Unexpected tab IDs: %s, %s", d.Tabs[0].ID, d.Tabs[1].ID)
	}
}
