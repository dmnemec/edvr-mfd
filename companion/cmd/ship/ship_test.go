package main

import (
	"context"
	"encoding/json"
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/writer"
)

func TestSeedFilesValidation(t *testing.T) {
	seeds := []string{
		filepath.Join("..", "..", "..", "packages", "ship", "screens", "modules.json"),
		filepath.Join("..", "..", "..", "packages", "ship", "screens", "power.json"),
		filepath.Join("..", "..", "..", "packages", "ship", "screens", "cargo.json"),
	}

	for _, path := range seeds {
		t.Run(filepath.Base(path), func(t *testing.T) {
			data, err := os.ReadFile(path)
			if err != nil {
				t.Fatalf("failed reading seed file %s: %v", path, err)
			}

			var d display.Display
			if err := json.Unmarshal(data, &d); err != nil {
				t.Fatalf("failed unmarshaling display JSON: %v", err)
			}

			if err := display.Validate(&d); err != nil {
				t.Fatalf("display validation failed: %v", err)
			}

			if d.StatusBadge == nil || d.StatusBadge.Text != "WAITING FOR DATA" {
				t.Fatalf("expected status badge 'WAITING FOR DATA', got %+v", d.StatusBadge)
			}
		})
	}

	manifestPath := filepath.Join("..", "..", "..", "packages", "ship", "manifest.json")
	mBytes, err := os.ReadFile(manifestPath)
	if err != nil {
		t.Fatalf("failed reading manifest: %v", err)
	}

	var manifest struct {
		ID       string `json:"id"`
		Name     string `json:"name"`
		Screens  []any  `json:"screens"`
		Companions []struct {
			ID  string `json:"id"`
			Exe string `json:"exe"`
		} `json:"companions"`
	}
	if err := json.Unmarshal(mBytes, &manifest); err != nil {
		t.Fatalf("failed unmarshaling manifest: %v", err)
	}

	if manifest.ID != "ship" || manifest.Name != "Ship Readout" {
		t.Errorf("unexpected manifest header: %+v", manifest)
	}
	if len(manifest.Screens) != 3 {
		t.Errorf("expected 3 screens in manifest, got %d", len(manifest.Screens))
	}
	if len(manifest.Companions) != 1 || manifest.Companions[0].ID != "ship-feed" {
		t.Errorf("expected companion 'ship-feed', got %+v", manifest.Companions)
	}
}

func TestModulesScreen_IntegrityAndSorting(t *testing.T) {
	pc := NewPriceCache(filepath.Join(t.TempDir(), "prices.json"))
	state := NewShipState(pc)

	loadout := elite.EventLoadout{
		Timestamp:    time.Now(),
		Event:        "Loadout",
		Ship:         "anaconda",
		ShipName:     "Voyager",
		ShipIdent:    "ED-001",
		HullHealth:   1.0,
		MaxJumpRange: 42.5,
		CargoCapacity: 128,
		FuelCapacity: elite.FuelCapacityInfo{Main: 32.0, Reserve: 0.58},
		Modules: []elite.LoadoutModule{
			// Core
			{Slot: "Armour", Item: "anaconda_armour_grade1", Health: 1.0, On: true, Priority: 1},
			{Slot: "PowerPlant", Item: "int_powerplant_size5_class5", Health: 0.75, On: true, Priority: 1}, // Damaged core (75% -> warn)
			{Slot: "MainEngines", Item: "int_engine_size7_class5", Health: 1.0, On: true, Priority: 1},
			{Slot: "FrameShiftDrive", Item: "int_hyperdrive_size6_class5", Health: 0.35, On: true, Priority: 1}, // Damaged core (35% -> crit)
			// Optional
			{Slot: "Slot01_Size6", Item: "int_shieldgenerator_size6_class5", Health: 1.0, On: true, Priority: 1},
			{Slot: "Slot02_Size5", Item: "int_cargorack_size5_class1", Health: 0.85, On: true, Priority: 1}, // Damaged optional (85% -> ok)
			// Hardpoint
			{Slot: "HugeHardpoint1", Item: "hpt_multicannon_gimbal_huge", Health: 1.0, On: false, Priority: 2}, // Powered off
			// Utility
			{Slot: "TinyHardpoint1", Item: "hpt_shieldbooster_size0_class5", Health: 0.50, On: true, Priority: 1}, // Damaged utility (50% -> warn)
			{Slot: "TinyHardpoint2", Item: "hpt_chafflauncher_tiny", Health: 1.0, On: true, Priority: 1},
		},
	}

	modChanged, _, _ := state.HandleJournalEntry(elite.JournalEntry{Event: "Loadout", Data: &loadout})
	if !modChanged {
		t.Fatalf("expected modulesChanged to be true on initial Loadout")
	}

	disp := state.BuildModulesDisplay()
	if err := display.Validate(disp); err != nil {
		t.Fatalf("generated modules display validation failed: %v", err)
	}

	// StatusBadge checks
	// Damaged modules: PowerPlant (0.75), FrameShiftDrive (0.35), Slot02 (0.85), TinyHardpoint1 (0.50) = 4 damaged
	// Min health = 0.35 (< 0.40) => color crit
	if disp.StatusBadge == nil || disp.StatusBadge.Text != "4 DAMAGED" || disp.StatusBadge.Color != display.PaletteCrit {
		t.Errorf("expected statusBadge '4 DAMAGED' (crit), got %+v", disp.StatusBadge)
	}

	// Integrity Tab ordering verification:
	// Damaged first sorted by health asc:
	// 1. FrameShiftDrive (0.35)
	// 2. TinyHardpoint1 (0.50)
	// 3. PowerPlant (0.75)
	// 4. Slot02_Size5 (0.85)
	// Followed by undamaged sorted by group (core -> optional -> hardpoint -> utility):
	// 5. Armour (core)
	// 6. MainEngines (core)
	// 7. Slot01_Size6 (optional)
	// 8. HugeHardpoint1 (hardpoint)
	// 9. TinyHardpoint2 (utility)
	integrityTab := disp.Tabs[0]
	if integrityTab.ID != "integrity" || len(integrityTab.Items) != 9 {
		t.Fatalf("unexpected integrity tab items length: %d", len(integrityTab.Items))
	}

	expectedOrder := []string{
		"slot_frameshiftdrive",
		"slot_tinyhardpoint1",
		"slot_powerplant",
		"slot_slot02_size5",
		"slot_armour",
		"slot_mainengines",
		"slot_slot01_size6",
		"slot_hugehardpoint1",
		"slot_tinyhardpoint2",
	}

	for i, expID := range expectedOrder {
		if integrityTab.Items[i].ID != expID {
			t.Errorf("at index %d expected %s, got %s", i, expID, integrityTab.Items[i].ID)
		}
	}

	// Check value colors and icons
	// FrameShiftDrive (0.35): crit color, warning icon
	fsdItem := integrityTab.Items[0]
	if fsdItem.Value != "35%" || fsdItem.ValueColor != display.PaletteCrit || fsdItem.Icon != display.IconWarning {
		t.Errorf("mismatched FSD item: %+v", fsdItem)
	}

	// TinyHardpoint1 (0.50): warn color, warning icon
	th1Item := integrityTab.Items[1]
	if th1Item.Value != "50%" || th1Item.ValueColor != display.PaletteWarn || th1Item.Icon != display.IconWarning {
		t.Errorf("mismatched TinyHardpoint1 item: %+v", th1Item)
	}

	// HugeHardpoint1 (100% health, but On=false): ok color, warning icon because turned off!
	hhItem := integrityTab.Items[7]
	if hhItem.Value != "100%" || hhItem.ValueColor != display.PaletteOK || hhItem.Icon != display.IconWarning {
		t.Errorf("mismatched powered-off HugeHardpoint1 item: %+v", hhItem)
	}

	// Tab 2: Hull Tab
	hullTab := disp.Tabs[1]
	if hullTab.ID != "hull" || len(hullTab.KeyValues) < 2 {
		t.Fatalf("unexpected hull tab: %+v", hullTab)
	}
	if hullTab.KeyValues[0].Value != "100%" || hullTab.KeyValues[1].Value != "4" {
		t.Errorf("mismatched hull KV: %+v", hullTab.KeyValues)
	}
}

func TestModulesScreen_DamageAndRepairEvents(t *testing.T) {
	pc := NewPriceCache(filepath.Join(t.TempDir(), "prices.json"))
	state := NewShipState(pc)

	loadout := elite.EventLoadout{
		Timestamp:  time.Now(),
		Event:      "Loadout",
		HullHealth: 1.0,
		Modules: []elite.LoadoutModule{
			{Slot: "PowerPlant", Item: "int_powerplant_size5_class5", Health: 1.0, On: true, Priority: 1},
			{Slot: "MainEngines", Item: "int_engine_size7_class5", Health: 1.0, On: true, Priority: 1},
		},
	}
	state.HandleJournalEntry(elite.JournalEntry{Event: "Loadout", Data: &loadout})

	disp1 := state.BuildModulesDisplay()
	if disp1.StatusBadge.Text != "NOMINAL" {
		t.Errorf("expected NOMINAL badge initially, got %s", disp1.StatusBadge.Text)
	}

	// 1. Take Hull Damage
	hullDmg := elite.EventHullDamage{Health: 0.75, Player: true}
	modChanged, _, _ := state.HandleJournalEntry(elite.JournalEntry{Event: "HullDamage", Data: &hullDmg})
	if !modChanged {
		t.Fatalf("expected modChanged on HullDamage")
	}

	// Duplicate HullDamage with same health should NOT report changed
	dupChanged, _, _ := state.HandleJournalEntry(elite.JournalEntry{Event: "HullDamage", Data: &hullDmg})
	if dupChanged {
		t.Fatalf("expected no modChanged on identical HullDamage")
	}

	// 2. AFMU repairs a module
	afmu := elite.EventAfmuRepairs{Module: "MainEngines", FullyRepaired: true, Health: 1.0}
	state.HandleJournalEntry(elite.JournalEntry{Event: "AfmuRepairs", Data: &afmu})

	// 3. Station repair all
	repairAll := elite.EventRepairAll{Cost: 1500}
	modChanged3, _, _ := state.HandleJournalEntry(elite.JournalEntry{Event: "RepairAll", Data: &repairAll})
	if !modChanged3 {
		t.Fatalf("expected modChanged on RepairAll")
	}

	dispRepaired := state.BuildModulesDisplay()
	if dispRepaired.StatusBadge.Text != "NOMINAL" {
		t.Errorf("expected NOMINAL after repair, got %s", dispRepaired.StatusBadge.Text)
	}
}

func TestPowerScreen_CapacityAndPips(t *testing.T) {
	pc := NewPriceCache(filepath.Join(t.TempDir(), "prices.json"))
	state := NewShipState(pc)

	// Loadout with standard 5A power plant (20.40 MW from table)
	loadout := elite.EventLoadout{
		Timestamp:    time.Now(),
		Event:        "Loadout",
		Ship:         "python",
		MaxJumpRange: 28.4,
		FuelCapacity: elite.FuelCapacityInfo{Main: 32.0, Reserve: 0.65},
		Modules: []elite.LoadoutModule{
			{Slot: "PowerPlant", Item: "int_powerplant_size5_class5", Health: 1.0, On: true, Priority: 1},
			{Slot: "Slot01_Size5", Item: "int_shieldgenerator_size5_class5", Health: 1.0, On: true, Priority: 1},
			{Slot: "Slot02_Size5", Item: "int_cargorack_size5_class1", Health: 1.0, On: true, Priority: 2},
		},
	}
	state.HandleJournalEntry(elite.JournalEntry{Event: "Loadout", Data: &loadout})

	if state.PowerPlantCapacity != 20.40 {
		t.Errorf("expected 20.40 MW power plant capacity, got %f", state.PowerPlantCapacity)
	}

	// Feed ModuleInfo.json with power draw and priorities
	modInfo := elite.ModuleInfoSnapshot{
		Modules: []elite.ModuleInfoEntry{
			{Slot: "PowerPlant", Item: "int_powerplant_size5_class5", Power: 0.0, Priority: 1},
			{Slot: "Slot01_Size5", Item: "int_shieldgenerator_size5_class5", Power: 3.25, Priority: 1},
			{Slot: "Slot02_Size5", Item: "int_cargorack_size5_class1", Power: 0.50, Priority: 2},
		},
	}
	state.HandleModuleInfo(modInfo)

	// Feed Status.json with pips and fuel
	status := elite.StatusSnapshot{
		Flags: elite.FlagInMainShip | elite.FlagOverheating,
		Pips:  elite.Pips{4, 2, 6}, // SYS: 4, ENG: 2, WEP: 6 half-pips
		Fuel: elite.StatusFuel{
			FuelMain:      24.0,
			FuelReservoir: 0.42,
		},
	}
	state.HandleStatus(status)

	disp := state.BuildPowerDisplay()
	if err := display.Validate(disp); err != nil {
		t.Fatalf("power display validation failed: %v", err)
	}

	// Status badge should reflect OVERHEATING
	if disp.StatusBadge == nil || disp.StatusBadge.Text != "OVERHEATING" || disp.StatusBadge.Color != display.PaletteCrit {
		t.Errorf("expected OVERHEATING badge, got %+v", disp.StatusBadge)
	}

	// Tab 1: Pips and gauges
	pipsTab := disp.Tabs[0]
	if pipsTab.ID != "pips" || len(pipsTab.Bars) != 6 {
		t.Fatalf("unexpected pips tab bars count: %d", len(pipsTab.Bars))
	}
	sysBar := pipsTab.Bars[0]
	if sysBar.Label != "SYS" || sysBar.Value != 4 || sysBar.Max != 8 {
		t.Errorf("mismatched SYS bar: %+v", sysBar)
	}
	wepBar := pipsTab.Bars[2]
	if wepBar.Label != "WEP" || wepBar.Value != 6 {
		t.Errorf("mismatched WEP bar: %+v", wepBar)
	}

	// Power draw bar: 3.25 + 0.50 = 3.75 MW out of 20.40 MW
	powerBar := pipsTab.Bars[3]
	if powerBar.Value != 3.75 || powerBar.Max != 20.40 {
		t.Errorf("mismatched power draw bar: %+v", powerBar)
	}

	// Tab 2: Status KV
	statusTab := disp.Tabs[1]
	if statusTab.ID != "status" {
		t.Fatalf("expected status tab, got %s", statusTab.ID)
	}
	jumpKV := statusTab.KeyValues[0]
	if jumpKV.Value != "28.40 LY" {
		t.Errorf("expected 28.40 LY jump range, got %s", jumpKV.Value)
	}
	heatKV := statusTab.KeyValues[1]
	if heatKV.Value != "CRITICAL" || heatKV.ValueColor != display.PaletteCrit {
		t.Errorf("expected CRITICAL overheating, got %+v", heatKV)
	}

	// Tab 3: Priorities list
	prioritiesTab := disp.Tabs[2]
	if prioritiesTab.ID != "priorities" || len(prioritiesTab.Items) != 3 {
		t.Fatalf("unexpected priorities tab: %+v", prioritiesTab)
	}
	if prioritiesTab.Items[0].Sublabel != "Priority 1 • Power Plant" && prioritiesTab.Items[0].Sublabel != "Priority 1 • Slot 01 (Size 5)" {
		t.Errorf("unexpected priority 1 item: %+v", prioritiesTab.Items[0])
	}
}

func TestPowerScreen_EngineeringPowerCapacityOverride(t *testing.T) {
	pc := NewPriceCache(filepath.Join(t.TempDir(), "prices.json"))
	state := NewShipState(pc)

	// Power plant with Engineering Overcharged modification
	loadout := elite.EventLoadout{
		Timestamp: time.Now(),
		Event:     "Loadout",
		Modules: []elite.LoadoutModule{
			{
				Slot:   "PowerPlant",
				Item:   "int_powerplant_size5_class5", // Table base is 20.40 MW
				Health: 1.0,
				On:     true,
				Engineering: &elite.ModuleEngineering{
					BlueprintName: "Overcharged",
					Level:         5,
					Modifiers: []elite.EngineeringModifier{
						{Label: "PowerCapacity", Value: 28.75}, // Override value!
					},
				},
			},
		},
	}
	state.HandleJournalEntry(elite.JournalEntry{Event: "Loadout", Data: &loadout})

	if state.PowerPlantCapacity != 28.75 {
		t.Fatalf("expected engineered capacity 28.75 MW, got %f", state.PowerPlantCapacity)
	}
}

func TestCargoScreen_PriceCacheAndCalculations(t *testing.T) {
	tmpDir := t.TempDir()
	cachePath := filepath.Join(tmpDir, "mean_prices.json")
	pc := NewPriceCache(cachePath)

	// 1. Visit station market and feed Market.json
	market := elite.MarketSnapshot{
		Items: []elite.MarketItem{
			{Name: "$gold_name;", NameLocalised: "Gold", MeanPrice: 48000},
			{Name: "$silver_name;", NameLocalised: "Silver", MeanPrice: 12500},
		},
	}
	if !pc.UpdateFromMarket(&market) {
		t.Fatalf("expected price cache to be updated")
	}

	// Verify persistence to disk
	if _, err := os.Stat(cachePath); err != nil {
		t.Fatalf("price cache file not written: %v", err)
	}

	// New cache instance reading same file
	pc2 := NewPriceCache(cachePath)
	if p, ok := pc2.Get("Gold"); !ok || p != 48000 {
		t.Errorf("expected 48000 for Gold, got %d, %v", p, ok)
	}

	state := NewShipState(pc)
	state.CargoCapacity = 64

	// 2. Feed Cargo.json with known and unknown commodities
	cargo := elite.CargoSnapshot{
		Vessel:        "Ship",
		Count:         15,
		CargoCapacity: 64,
		Inventory: []elite.CargoItem{
			{Name: "gold", NameLocalised: "Gold", Count: 10, Stolen: 0},
			{Name: "silver", NameLocalised: "Silver", Count: 3, Stolen: 1},
			{Name: "biowaste", NameLocalised: "Biowaste", Count: 2, Stolen: 0}, // Unknown price
		},
	}
	state.HandleCargo(cargo)

	disp := state.BuildCargoDisplay()
	if err := display.Validate(disp); err != nil {
		t.Fatalf("cargo display validation failed: %v", err)
	}

	// Status badge should show 15 / 64 T
	if disp.StatusBadge == nil || disp.StatusBadge.Text != "15 / 64 T" || disp.StatusBadge.Color != display.PaletteOK {
		t.Errorf("mismatched cargo badge: %+v", disp.StatusBadge)
	}

	// Tab 1: Inventory list
	invTab := disp.Tabs[0]
	if invTab.ID != "inventory" || len(invTab.Items) != 3 {
		t.Fatalf("unexpected cargo inventory items: %d", len(invTab.Items))
	}

	// Gold: 10 units @ 48,000 CR
	goldItem := invTab.Items[0]
	if goldItem.Label != "Gold" || goldItem.Sublabel != "10 T" || goldItem.Value != "48,000 CR" {
		t.Errorf("mismatched Gold item: %+v", goldItem)
	}

	// Silver: 3 units (1 Stolen) -> icon lock
	silverItem := invTab.Items[1]
	if silverItem.Icon != display.IconLock || silverItem.Sublabel != "3 T (1 Stolen)" {
		t.Errorf("mismatched Silver item: %+v", silverItem)
	}

	// Biowaste: unknown price -> value "—", color dim
	bioItem := invTab.Items[2]
	if bioItem.Value != "—" || bioItem.ValueColor != display.PaletteDim {
		t.Errorf("mismatched Biowaste item: %+v", bioItem)
	}

	// Tab 2: Overview KV
	// Total value = 10 * 48000 + 3 * 12500 = 480,000 + 37,500 = 517,500 CR+ (suffix + due to unknown biowaste)
	overviewTab := disp.Tabs[1]
	totalValKV := overviewTab.KeyValues[1]
	if totalValKV.Key != "Total Est. Value" || totalValKV.Value != "517,500 CR+" {
		t.Errorf("mismatched total value KV: %+v", totalValKV)
	}

	// Tab 3: Capacity bar
	capTab := disp.Tabs[2]
	if capTab.ID != "capacity" || len(capTab.Bars) != 1 {
		t.Fatalf("unexpected capacity tab: %+v", capTab)
	}
	bar := capTab.Bars[0]
	if bar.Value != 15 || bar.Max != 64 {
		t.Errorf("mismatched capacity bar: %+v", bar)
	}
}

func TestEngine_EndToEndPipeline(t *testing.T) {
	tempPkgDir := t.TempDir()
	tempJournalDir := t.TempDir()

	modulesDest := filepath.Join(tempPkgDir, "screens", "modules.json")
	powerDest := filepath.Join(tempPkgDir, "screens", "power.json")
	cargoDest := filepath.Join(tempPkgDir, "screens", "cargo.json")

	cfg := Config{
		PackageDir:     tempPkgDir,
		JournalDir:     tempJournalDir,
		PriceCachePath: filepath.Join(tempPkgDir, "prices.json"),
		CoalesceRate:   20 * time.Millisecond,
		PollInterval:   10 * time.Millisecond,
		ModulesFile:    modulesDest,
		PowerFile:      powerDest,
		CargoFile:      cargoDest,
	}

	cw := writer.NewCoalescedWriter(writer.WithInterval(cfg.CoalesceRate))
	eng := NewEngine(cfg, cw)

	// 1. Create initial journal with Loadout
	jFile := filepath.Join(tempJournalDir, "Journal.2026-10-15T120000.01.log")
	jContent := `{"timestamp":"2026-10-15T12:00:00Z","event":"LoadGame","Commander":"Jameson"}` + "\n" +
		`{"timestamp":"2026-10-15T12:00:01Z","event":"Loadout","Ship":"anaconda","HullHealth":1.0,"CargoCapacity":64,"MaxJumpRange":35.0,"FuelCapacity":{"Main":32.0,"Reserve":0.5},"Modules":[{"Slot":"PowerPlant","Item":"int_powerplant_size5_class5","Health":1.0,"On":true,"Priority":1}]}` + "\n"
	if err := os.WriteFile(jFile, []byte(jContent), 0644); err != nil {
		t.Fatal(err)
	}

	// 2. Create snapshot files
	statusFile := filepath.Join(tempJournalDir, "Status.json")
	statusContent := `{"timestamp":"2026-10-15T12:00:02Z","event":"Status","Flags":16777216,"Pips":[4,4,4],"Fuel":{"FuelMain":32.0,"FuelReservoir":0.5}}` + "\n"
	if err := os.WriteFile(statusFile, []byte(statusContent), 0644); err != nil {
		t.Fatal(err)
	}

	cargoFile := filepath.Join(tempJournalDir, "Cargo.json")
	cargoContent := `{"timestamp":"2026-10-15T12:00:02Z","event":"Cargo","Vessel":"Ship","Count":5,"CargoCapacity":64,"Inventory":[{"Name":"gold","Count":5,"Stolen":0}]}` + "\n"
	if err := os.WriteFile(cargoFile, []byte(cargoContent), 0644); err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	errCh := make(chan error, 1)
	go func() {
		errCh <- eng.Run(ctx)
	}()

	// Wait for screens to be written to disk
	deadline := time.Now().Add(1500 * time.Millisecond)
	for time.Now().Before(deadline) {
		_, errM := os.Stat(modulesDest)
		_, errP := os.Stat(powerDest)
		_, errC := os.Stat(cargoDest)
		if errM == nil && errP == nil && errC == nil {
			break
		}
		time.Sleep(25 * time.Millisecond)
	}

	cancel()
	_ = <-errCh

	// Validate written files
	for _, screenPath := range []string{modulesDest, powerDest, cargoDest} {
		data, err := os.ReadFile(screenPath)
		if err != nil {
			t.Fatalf("screen file %s not written: %v", screenPath, err)
		}
		var d display.Display
		if err := json.Unmarshal(data, &d); err != nil {
			t.Fatalf("invalid JSON in %s: %v", screenPath, err)
		}
		if err := display.Validate(&d); err != nil {
			t.Fatalf("display validation failed for %s: %v", screenPath, err)
		}
	}
}
