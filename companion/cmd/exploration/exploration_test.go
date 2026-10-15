package main

import (
	"context"
	"encoding/json"
	"math"
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

func TestSeedFilesValidation(t *testing.T) {
	for _, screenFile := range []string{"system.json", "bio.json"} {
		t.Run(screenFile, func(t *testing.T) {
			path := filepath.Join("..", "..", "..", "packages", "exploration", "screens", screenFile)
			data, err := os.ReadFile(path)
			if err != nil {
				t.Fatalf("failed to read seed file %s: %v", path, err)
			}

			var disp display.Display
			if err := json.Unmarshal(data, &disp); err != nil {
				t.Fatalf("failed to unmarshal seed file %s: %v", path, err)
			}

			if err := display.Validate(&disp); err != nil {
				t.Fatalf("seed file %s failed schema validation: %v", path, err)
			}

			if disp.StatusBadge == nil || disp.StatusBadge.Text != "WAITING FOR SCAN" {
				t.Errorf("expected WAITING FOR SCAN badge, got %+v", disp.StatusBadge)
			}
		})
	}
}

func TestBodyValueCalculation(t *testing.T) {
	// Stars
	wdVal := CalculateBodyValue("", "D", "", 0.6, true, false, false)
	if wdVal < 14000 {
		t.Errorf("expected white dwarf value > 14000, got %d", wdVal)
	}

	nsVal := CalculateBodyValue("", "N", "", 1.4, false, false, false)
	if nsVal < 50000 { // 22628 * 2.6 ~ 58832
		t.Errorf("expected first discovered neutron star value > 50000, got %d", nsVal)
	}

	// Planets: ELW
	elwDiscovered := CalculateBodyValue("Earthlike body", "", "", 1.0, true, false, false)
	elwFirstDiscMap := CalculateBodyValue("Earthlike body", "", "", 1.0, false, false, true)
	if elwFirstDiscMap <= elwDiscovered*5 {
		t.Errorf("expected first discovery + mapped ELW to be substantially higher: %d vs %d", elwFirstDiscMap, elwDiscovered)
	}

	// Terraformable HMC vs non-terraformable HMC
	hmcVal := CalculateBodyValue("High metal content body", "", "", 0.8, true, false, false)
	hmcTerraVal := CalculateBodyValue("High metal content body", "", "Terraformable", 0.8, true, false, false)
	if hmcTerraVal <= hmcVal*2 {
		t.Errorf("expected terraformable HMC to be substantially higher than non-terraformable: %d vs %d", hmcTerraVal, hmcVal)
	}
}

func TestHaversineAndColonyDistance(t *testing.T) {
	radius := 6371000.0 // Earth radius in meters
	// 0.001 degrees latitude is ~111.19 meters
	dist := HaversineDistance(0.0, 0.0, 0.001, 0.0, radius)
	if math.Abs(dist-111.19) > 1.0 {
		t.Errorf("expected ~111.19m for 0.001 deg lat, got %.2fm", dist)
	}

	// Colony range table check
	if r := GetGenusColonyRange("Bacterium"); r != 500.0 {
		t.Errorf("expected 500m for Bacterium, got %.1f", r)
	}
	if r := GetGenusColonyRange("Cactoida"); r != 300.0 {
		t.Errorf("expected 300m for Cactoida, got %.1f", r)
	}
	if r := GetGenusColonyRange("Aleoida"); r != 150.0 {
		t.Errorf("expected 150m for Aleoida, got %.1f", r)
	}
	if r := GetGenusColonyRange("Electricae"); r != 1000.0 {
		t.Errorf("expected 1000m for Electricae, got %.1f", r)
	}
}

func TestSpeciesBaseValues(t *testing.T) {
	val := GetSpeciesBaseValue("Stratum Tectonicas")
	if val != 19010800 {
		t.Errorf("expected 19010800 for Stratum Tectonicas, got %d", val)
	}

	valBact := GetSpeciesBaseValue("Bacterium Aurasus")
	if valBact != 1000000 {
		t.Errorf("expected 1000000 for Bacterium Aurasus, got %d", valBact)
	}
}

func TestStatePersistenceAndSwitchSystem(t *testing.T) {
	tmpDir := t.TempDir()
	st := NewSystemState(tmpDir)

	// 1. Jump to System A
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventFSDJump{
			SystemAddress:          1001,
			StarSystem:             "Alpha Centauri",
			SystemAllegiance:       "Federation",
			SystemSecurityLocalised: "High",
			Population:             150000,
		},
	})

	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventScan{
			SystemAddress:         1001,
			BodyID:                1,
			BodyName:              "Alpha Centauri A",
			StarType:              "G",
			DistanceFromArrivalLS: 0,
			ScanType:              "Detailed",
		},
	})
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventScan{
			SystemAddress:         1001,
			BodyID:                2,
			BodyName:              "Alpha Centauri A 1",
			PlanetClass:           "High metal content body",
			DistanceFromArrivalLS: 150,
			ScanType:              "Detailed",
			WasDiscovered:         true,
			WasMapped:             true,
		},
	})

	if len(st.Bodies) != 2 {
		t.Fatalf("expected 2 bodies in System 1001, got %d", len(st.Bodies))
	}
	_ = st.Save()

	// 2. Jump to System B
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventFSDJump{
			SystemAddress:          2002,
			StarSystem:             "Sol",
			SystemAllegiance:       "Federation",
			SystemSecurityLocalised: "High",
			Population:             50000000000,
		},
	})

	if st.SystemAddress != 2002 {
		t.Fatalf("expected current system address 2002, got %d", st.SystemAddress)
	}
	if len(st.Bodies) != 0 {
		t.Fatalf("expected bodies to reset on jump, got %d", len(st.Bodies))
	}

	// 3. Jump back to System A (revisit restores progress §12.2)
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventFSDJump{
			SystemAddress:          1001,
			StarSystem:             "Alpha Centauri",
			SystemAllegiance:       "Federation",
			SystemSecurityLocalised: "High",
			Population:             150000,
		},
	})

	if st.SystemAddress != 1001 {
		t.Fatalf("expected current system address 1001, got %d", st.SystemAddress)
	}
	if len(st.Bodies) != 2 {
		t.Fatalf("expected restored 2 bodies in System 1001, got %d", len(st.Bodies))
	}
}

func TestSystemScreen_OverviewAndBodiesWithChecks(t *testing.T) {
	tmpDir := t.TempDir()
	st := NewSystemState(tmpDir)

	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventFSDJump{
			SystemAddress:          3003,
			StarSystem:             "Epsilon Eridani",
			SystemAllegiance:       "Independent",
			SystemSecurityLocalised: "Medium",
			Population:             450000,
		},
	})
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventFSSDiscoveryScan{
			SystemAddress: 3003,
			BodyCount:     5,
		},
	})

	// Add primary star
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventScan{
			SystemAddress:         3003,
			BodyID:                0,
			BodyName:              "Epsilon Eridani",
			StarType:              "K",
			DistanceFromArrivalLS: 0,
			ScanType:              "Detailed",
		},
	})

	// Add Earthlike body (pilot mapped, no bio)
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventScan{
			SystemAddress:         3003,
			BodyID:                1,
			BodyName:              "Epsilon Eridani 1",
			PlanetClass:           "Earthlike body",
			DistanceFromArrivalLS: 240,
			Landable:              false,
			ScanType:              "Detailed",
			WasDiscovered:         true,
			WasMapped:             true,
		},
	})
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventSAAScanComplete{
			SystemAddress:    3003,
			BodyID:           1,
			BodyName:         "Epsilon Eridani 1",
			ProbesUsed:       4,
			EfficiencyTarget: 6,
		},
	})

	// Add landable body with bio and geo signals
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventScan{
			SystemAddress:         3003,
			BodyID:                2,
			BodyName:              "Epsilon Eridani 2",
			PlanetClass:           "High metal content body",
			DistanceFromArrivalLS: 580,
			Landable:              true,
			TerraformState:        "Terraformable",
			ScanType:              "Detailed",
			WasDiscovered:         true,
			WasMapped:             false,
		},
	})
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventFSSBodySignals{
			SystemAddress: 3003,
			BodyID:        2,
			BodyName:      "Epsilon Eridani 2",
			Signals: []elite.BodySignal{
				{Type: "$SAA_SignalType_Biological;", Count: 2},
				{Type: "$SAA_SignalType_Geological;", Count: 1},
			},
		},
	})

	disp := st.BuildSystemDisplay()
	if err := display.Validate(disp); err != nil {
		t.Fatalf("BuildSystemDisplay generated invalid display: %v", err)
	}

	if disp.Title != "SYSTEM" || disp.Subtitle != "Epsilon Eridani" {
		t.Errorf("unexpected title/subtitle: %s / %s", disp.Title, disp.Subtitle)
	}

	// Verify Overview tab
	overviewTab := disp.Tabs[0]
	if overviewTab.ID != "overview" {
		t.Fatalf("expected overview tab first, got %s", overviewTab.ID)
	}

	var bodiesScannedVal string
	for _, kv := range overviewTab.KeyValues {
		if kv.ID == "bodies_scanned" {
			bodiesScannedVal = kv.Value
		}
	}
	if !strings.Contains(bodiesScannedVal, "3 / 5") {
		t.Errorf("expected bodies scanned '3 / 5', got %s", bodiesScannedVal)
	}

	// Verify Bodies tab
	bodiesTab := disp.Tabs[1]
	if bodiesTab.ID != "bodies" {
		t.Fatalf("expected bodies tab second, got %s", bodiesTab.ID)
	}

	// Check items: 3 scanned bodies + 1 unscanned gap item
	if len(bodiesTab.Items) != 4 {
		t.Fatalf("expected 4 items in bodies tab, got %d", len(bodiesTab.Items))
	}

	// Check trailing unscanned gap item
	gapItem := bodiesTab.Items[len(bodiesTab.Items)-1]
	if gapItem.ID != "unscanned_gap" || !strings.Contains(gapItem.Label, "2") {
		t.Errorf("expected unscanned gap with 2 bodies, got %+v", gapItem)
	}

	// Verify checks on body 1 (ELW, mapped by pilot)
	var body1Item *display.Item
	for i := range bodiesTab.Items {
		if bodiesTab.Items[i].ID == "body_1" {
			body1Item = &bodiesTab.Items[i]
			break
		}
	}
	if body1Item == nil {
		t.Fatal("body_1 not found in bodies tab")
	}
	// Check S (done), M (done), B (na), G (na)
	expectedChecks1 := []display.Check{
		{Label: "S", State: display.CheckStateDone},
		{Label: "M", State: display.CheckStateDone},
		{Label: "B", State: display.CheckStateNA},
		{Label: "G", State: display.CheckStateNA},
	}
	for i, c := range body1Item.Checks {
		if c.Label != expectedChecks1[i].Label || c.State != expectedChecks1[i].State {
			t.Errorf("mismatch on body 1 check %d: expected %+v, got %+v", i, expectedChecks1[i], c)
		}
	}

	// Verify checks and tags on body 2 (HMC, Landable, Terraformable, 2 bio, 1 geo)
	var body2Item *display.Item
	for i := range bodiesTab.Items {
		if bodiesTab.Items[i].ID == "body_2" {
			body2Item = &bodiesTab.Items[i]
			break
		}
	}
	if body2Item == nil {
		t.Fatal("body_2 not found in bodies tab")
	}
	if !strings.Contains(body2Item.Sublabel, "LAND") || !strings.Contains(body2Item.Sublabel, "TERRA") {
		t.Errorf("expected LAND and TERRA tags in sublabel, got %s", body2Item.Sublabel)
	}
	if body2Item.Icon != display.IconBio {
		t.Errorf("expected bio icon for body 2, got %s", body2Item.Icon)
	}
	// Check S (done), M (todo), B (todo), G (done)
	expectedChecks2 := []display.Check{
		{Label: "S", State: display.CheckStateDone},
		{Label: "M", State: display.CheckStateTodo},
		{Label: "B", State: display.CheckStateTodo},
		{Label: "G", State: display.CheckStateDone},
	}
	for i, c := range body2Item.Checks {
		if c.Label != expectedChecks2[i].Label || c.State != expectedChecks2[i].State {
			t.Errorf("mismatch on body 2 check %d: expected %+v, got %+v", i, expectedChecks2[i], c)
		}
	}
}

func TestSystemScreen_SelectBodyDetailTab(t *testing.T) {
	tmpDir := t.TempDir()
	st := NewSystemState(tmpDir)

	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventFSDJump{
			SystemAddress: 4004,
			StarSystem:    "Vega",
		},
	})
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventScan{
			SystemAddress:         4004,
			BodyID:                3,
			BodyName:              "Vega 3",
			PlanetClass:           "High metal content body",
			SurfaceGravity:        14.7,
			Atmosphere:            "Carbon dioxide",
			SurfaceTemperature:    320.0,
			SurfacePressure:       1.5,
			ScanType:              "Detailed",
			WasDiscovered:         true,
			WasMapped:             false,
		},
	})

	// User selects Vega 3 via MFD UI
	st.HandleEvent(events.Event{
		Action: "select",
		ItemID: "body_3",
	})

	disp := st.BuildSystemDisplay()
	if err := display.Validate(disp); err != nil {
		t.Fatalf("BuildSystemDisplay with detail tab invalid: %v", err)
	}

	// Should now have 3 tabs: overview, bodies, body
	if len(disp.Tabs) != 3 || disp.Tabs[2].ID != "body" {
		t.Fatalf("expected 3rd tab to be detail tab 'body', got %d tabs", len(disp.Tabs))
	}

	detailTab := disp.Tabs[2]
	var foundAtmo, foundTemp bool
	for _, kv := range detailTab.KeyValues {
		if kv.Key == "Atmosphere" && kv.Value == "Carbon dioxide" {
			foundAtmo = true
		}
		if kv.Key == "Surface Temp" && strings.Contains(kv.Value, "320 K") {
			foundTemp = true
		}
	}
	if !foundAtmo || !foundTemp {
		t.Errorf("expected atmosphere and temperature in detail tab: atmo=%v, temp=%v", foundAtmo, foundTemp)
	}
}

func TestBioScreen_StagesAndLiveSampleDistance(t *testing.T) {
	tmpDir := t.TempDir()
	st := NewSystemState(tmpDir)

	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventFSDJump{
			SystemAddress: 5005,
			StarSystem:    "Procyon",
		},
	})

	// Add body with bio signals and genuses
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventScan{
			SystemAddress: 5005,
			BodyID:        4,
			BodyName:      "Procyon B 1",
			PlanetClass:   "Rocky body",
			Radius:        3200000,
			Landable:      true,
			WasFootfalled: false, // First footfall candidate (5x exobio value!)
		},
	})
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventSAASignalsFound{
			SystemAddress: 5005,
			BodyID:        4,
			BodyName:      "Procyon B 1",
			Genuses: []elite.GenusSignal{
				{Genus: "$Codex_Ent_Bacterial_Genus_Name;", GenusLocalised: "Bacterium"},
				{Genus: "$Codex_Ent_Stratum_Genus_Name;", GenusLocalised: "Stratum"},
			},
		},
	})

	// Pilot approaches body
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventApproachBody{
			SystemAddress: 5005,
			StarSystem:    "Procyon",
			BodyID:        4,
			Body:          "Procyon B 1",
		},
	})

	// Status.json coordinates update on landing
	lat := 10.0000
	lon := 20.0000
	radius := 3200000.0
	st.HandleStatus(elite.StatusSnapshot{
		Latitude:     &lat,
		Longitude:    &lon,
		PlanetRadius: &radius,
		Flags2:       elite.Flag2OnFoot,
	})

	// 1. Log stage on Stratum Tectonicas
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventScanOrganic{
			SystemAddress:    5005,
			Body:             4,
			ScanType:         "Log",
			GenusLocalised:   "Stratum",
			SpeciesLocalised: "Stratum Tectonicas",
		},
	})

	disp1 := st.BuildBioDisplay()
	if err := display.Validate(disp1); err != nil {
		t.Fatalf("bio display after Log invalid: %v", err)
	}

	// Current tab should be first because we are on/near body 4
	if disp1.Tabs[0].ID != "current" {
		t.Fatalf("expected current tab first on active body, got %s", disp1.Tabs[0].ID)
	}

	// Verify Stratum Tectonicas row in current tab
	currentTab := disp1.Tabs[0]
	var stratumItem *display.Item
	for i := range currentTab.Items {
		if strings.Contains(currentTab.Items[i].Label, "Stratum Tectonicas") {
			stratumItem = &currentTab.Items[i]
			break
		}
	}
	if stratumItem == nil {
		t.Fatal("Stratum Tectonicas not found in current tab")
	}
	if stratumItem.Badge != "LOGGED" {
		t.Errorf("expected LOGGED badge, got %s", stratumItem.Badge)
	}
	// Value should be 19,010,800 * 5 = 95,054,000 CR
	if !strings.Contains(stratumItem.Value, "95,054,000") {
		t.Errorf("expected 95,054,000 CR (5x first footfall), got %s", stratumItem.Value)
	}
	// Check L (done), S (todo), A (todo)
	if stratumItem.Checks[0].State != display.CheckStateDone || stratumItem.Checks[1].State != display.CheckStateTodo {
		t.Errorf("unexpected check states: %+v", stratumItem.Checks)
	}

	// 2. Sample tab live distance check: move 200m away (Stratum colony req is 500m)
	// At lat 10 deg, ~0.002 deg is ~111m on 3200km radius
	// Moving 0.003 degrees latitude is ~167m (< 500m required)
	latTooClose := 10.0030
	st.HandleStatus(elite.StatusSnapshot{
		Latitude:     &latTooClose,
		Longitude:    &lon,
		PlanetRadius: &radius,
		Flags2:       elite.Flag2OnFoot,
	})

	dispTooClose := st.BuildBioDisplay()
	sampleTab := dispTooClose.Tabs[1] // sample tab is 2nd tab when on body
	var distKV, statusKV *display.KeyValue
	for i := range sampleTab.KeyValues {
		if sampleTab.KeyValues[i].ID == "sample_dist" {
			distKV = &sampleTab.KeyValues[i]
		}
		if sampleTab.KeyValues[i].ID == "sample_status" {
			statusKV = &sampleTab.KeyValues[i]
		}
	}
	if distKV == nil || statusKV == nil {
		t.Fatal("missing sample_dist or sample_status in sample tab")
	}
	if distKV.ValueColor != display.PaletteCrit || !strings.Contains(statusKV.Value, "TOO CLOSE") {
		t.Errorf("expected crit color and TOO CLOSE status, got %s / %s", distKV.ValueColor, statusKV.Value)
	}

	// Move far enough (> 500m, e.g. 0.012 degrees ~ 670m)
	latFar := 10.0120
	st.HandleStatus(elite.StatusSnapshot{
		Latitude:     &latFar,
		Longitude:    &lon,
		PlanetRadius: &radius,
		Flags2:       elite.Flag2OnFoot,
	})

	dispFar := st.BuildBioDisplay()
	sampleTabFar := dispFar.Tabs[1]
	for i := range sampleTabFar.KeyValues {
		if sampleTabFar.KeyValues[i].ID == "sample_dist" {
			distKV = &sampleTabFar.KeyValues[i]
		}
		if sampleTabFar.KeyValues[i].ID == "sample_status" {
			statusKV = &sampleTabFar.KeyValues[i]
		}
	}
	if distKV.ValueColor != display.PaletteOK || !strings.Contains(statusKV.Value, "DIVERSE COLONY READY") {
		t.Errorf("expected OK color and DIVERSE COLONY READY, got %s / %s", distKV.ValueColor, statusKV.Value)
	}

	// 3. Sample 2 stage
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventScanOrganic{
			SystemAddress:    5005,
			Body:             4,
			ScanType:         "Sample",
			GenusLocalised:   "Stratum",
			SpeciesLocalised: "Stratum Tectonicas",
		},
	})
	dispSample2 := st.BuildBioDisplay()
	for i := range dispSample2.Tabs[0].Items {
		if strings.Contains(dispSample2.Tabs[0].Items[i].Label, "Stratum Tectonicas") {
			stratumItem = &dispSample2.Tabs[0].Items[i]
			break
		}
	}
	if stratumItem.Badge != "2/3 SAMPLED" {
		t.Errorf("expected 2/3 SAMPLED badge, got %s", stratumItem.Badge)
	}
	if stratumItem.Checks[0].State != display.CheckStateDone || stratumItem.Checks[1].State != display.CheckStateDone || stratumItem.Checks[2].State != display.CheckStateTodo {
		t.Errorf("expected L:done, S:done, A:todo, got %+v", stratumItem.Checks)
	}

	// 4. Analyse stage (Complete)
	st.HandleJournalEntry(elite.JournalEntry{
		Data: &elite.EventScanOrganic{
			SystemAddress:    5005,
			Body:             4,
			ScanType:         "Analyse",
			GenusLocalised:   "Stratum",
			SpeciesLocalised: "Stratum Tectonicas",
		},
	})
	dispAnalyse := st.BuildBioDisplay()
	for i := range dispAnalyse.Tabs[0].Items {
		if strings.Contains(dispAnalyse.Tabs[0].Items[i].Label, "Stratum Tectonicas") {
			stratumItem = &dispAnalyse.Tabs[0].Items[i]
			break
		}
	}
	if stratumItem.Badge != "ANALYSED" {
		t.Errorf("expected ANALYSED badge, got %s", stratumItem.Badge)
	}
	if stratumItem.Checks[0].State != display.CheckStateDone || stratumItem.Checks[1].State != display.CheckStateDone || stratumItem.Checks[2].State != display.CheckStateDone {
		t.Errorf("expected L:done, S:done, A:done, got %+v", stratumItem.Checks)
	}
}

func TestEngineEndToEnd(t *testing.T) {
	pkgDir := t.TempDir()
	journalDir := t.TempDir()
	systemsDir := filepath.Join(pkgDir, "systems")

	sysFile := filepath.Join(pkgDir, "screens", "system.json")
	bioFile := filepath.Join(pkgDir, "screens", "bio.json")
	eventsFile := filepath.Join(pkgDir, "events.jsonl")

	_ = os.MkdirAll(filepath.Join(pkgDir, "screens"), 0o755)

	cfg := Config{
		PackageDir:       pkgDir,
		JournalDir:       journalDir,
		SystemsDir:       systemsDir,
		EventsPath:       eventsFile,
		SystemScreenFile: sysFile,
		BioScreenFile:    bioFile,
		CoalesceRate:     10 * time.Millisecond,
		PollInterval:     10 * time.Millisecond,
	}

	w := writer.NewCoalescedWriter(writer.WithInterval(10 * time.Millisecond))
	eng := NewEngine(cfg, w)

	ctx, cancel := context.WithCancel(context.Background())
	defer cancel()

	// Initial journal file
	jFile := filepath.Join(journalDir, "Journal.2026-10-15T120000.01.log")
	initialContent := strings.Join([]string{
		`{"timestamp":"2026-10-15T12:00:00Z","event":"Fileheader","part":1,"language":"English/UK","gameversion":"4.0.0.100","build":"r12345"}`,
		`{"timestamp":"2026-10-15T12:01:00Z","event":"FSDJump","StarSystem":"Barnard's Star","SystemAddress":6006,"SystemAllegiance":"Federation","SystemSecurity_Localised":"High","Population":300000}`,
		`{"timestamp":"2026-10-15T12:02:00Z","event":"FSSDiscoveryScan","SystemAddress":6006,"BodyCount":3}`,
		`{"timestamp":"2026-10-15T12:03:00Z","event":"Scan","SystemAddress":6006,"BodyID":1,"BodyName":"Barnard's Star 1","PlanetClass":"High metal content body","ScanType":"Detailed","WasDiscovered":true}`,
	}, "\n") + "\n"
	_ = os.WriteFile(jFile, []byte(initialContent), 0o644)

	errCh := make(chan error, 1)
	go func() {
		errCh <- eng.Run(ctx)
	}()

	// Wait for engine to process initial journal entries and write screens
	time.Sleep(100 * time.Millisecond)
	_ = eng.FlushScreens()

	// Verify system.json was written and is valid
	sysData, err := os.ReadFile(sysFile)
	if err != nil {
		t.Fatalf("failed to read written system.json: %v", err)
	}
	var sysDisp display.Display
	if err := json.Unmarshal(sysData, &sysDisp); err != nil {
		t.Fatalf("failed to unmarshal written system.json: %v", err)
	}
	if err := display.Validate(&sysDisp); err != nil {
		t.Fatalf("written system.json failed schema validation: %v", err)
	}
	if sysDisp.Subtitle != "Barnard's Star" {
		t.Errorf("expected subtitle 'Barnard's Star', got %s", sysDisp.Subtitle)
	}

	cancel()
	select {
	case err := <-errCh:
		if err != nil && err != context.Canceled {
			t.Errorf("engine returned unexpected error: %v", err)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("engine failed to shut down within 2s")
	}
}
