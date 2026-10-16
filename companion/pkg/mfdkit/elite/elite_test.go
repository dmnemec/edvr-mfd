package elite

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

func TestDiscovery(t *testing.T) {
	tempDir := t.TempDir()

	// 1. Env var override
	t.Setenv(EnvJournalDir, tempDir)
	discovered, err := DiscoverJournalDir()
	if err != nil {
		t.Fatalf("DiscoverJournalDir failed: %v", err)
	}
	if discovered != tempDir {
		t.Fatalf("expected discovered dir %q, got %q", tempDir, discovered)
	}

	// 2. Default dir fallback
	t.Setenv(EnvJournalDir, "")
	defaultDir, err := DiscoverJournalDir()
	if err != nil {
		t.Fatalf("DiscoverJournalDir fallback failed: %v", err)
	}
	if !strings.Contains(defaultDir, "Elite Dangerous") {
		t.Fatalf("expected fallback to contain 'Elite Dangerous', got %q", defaultDir)
	}

	// 3. File pattern matching and listing
	j1 := filepath.Join(tempDir, "Journal.2026-10-15T100000.01.log")
	j2 := filepath.Join(tempDir, "Journal.2026-10-15T120000.01.log")
	other := filepath.Join(tempDir, "Status.json")

	if err := os.WriteFile(j1, []byte("{}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(j2, []byte("{}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(other, []byte("{}\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	files, err := ListJournalFiles(tempDir)
	if err != nil {
		t.Fatalf("ListJournalFiles failed: %v", err)
	}
	if len(files) != 2 {
		t.Fatalf("expected 2 journal files, got %d", len(files))
	}
	if files[0] != j1 || files[1] != j2 {
		t.Fatalf("unexpected sort order: %v", files)
	}

	latest, err := FindLatestJournal(tempDir)
	if err != nil {
		t.Fatalf("FindLatestJournal failed: %v", err)
	}
	if latest != j2 {
		t.Fatalf("expected latest %q, got %q", j2, latest)
	}
}

func TestStronglyTypedEventsParsing(t *testing.T) {
	testCases := []struct {
		name     string
		jsonLine string
		check    func(t *testing.T, entry JournalEntry)
	}{
		// Ship / Loadout / Modules
		{
			name: "Loadout",
			jsonLine: `{
				"timestamp": "2026-10-15T12:00:00Z",
				"event": "Loadout",
				"Ship": "anaconda",
				"ShipID": 1,
				"ShipName": "Voyager",
				"ShipIdent": "ED-001",
				"HullHealth": 0.95,
				"CargoCapacity": 128,
				"MaxJumpRange": 45.2,
				"FuelCapacity": {"Main": 32.0, "Reserve": 0.58},
				"Modules": [
					{
						"Slot": "HugeHardpoint1",
						"Item": "hpt_multicannon_gimbal_huge",
						"On": true,
						"Priority": 1,
						"Health": 1.0,
						"Engineering": {
							"BlueprintName": "Overcharged",
							"Level": 5,
							"Quality": 1.0,
							"Modifiers": [
								{"Label": "PowerCapacity", "Value": 35.5}
							]
						}
					}
				]
			}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventLoadout)
				if !ok {
					t.Fatalf("expected *EventLoadout, got %T", entry.Data)
				}
				if ev.Ship != "anaconda" || ev.CargoCapacity != 128 || ev.HullHealth != 0.95 {
					t.Errorf("mismatched Loadout fields: %+v", ev)
				}
				if len(ev.Modules) != 1 || ev.Modules[0].Engineering == nil || ev.Modules[0].Engineering.BlueprintName != "Overcharged" {
					t.Errorf("mismatched Module engineering: %+v", ev.Modules)
				}
			},
		},
		{
			name: "HullDamage",
			jsonLine: `{"timestamp":"2026-10-15T12:01:00Z","event":"HullDamage","Health":0.82,"Player":true,"Fighter":false}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventHullDamage)
				if !ok || ev.Health != 0.82 || !ev.Player {
					t.Errorf("mismatched HullDamage: %+v", entry.Data)
				}
			},
		},
		{
			name: "AfmuRepairs",
			jsonLine: `{"timestamp":"2026-10-15T12:02:00Z","event":"AfmuRepairs","Module":"HugeHardpoint1","FullyRepaired":true,"Health":1.0}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventAfmuRepairs)
				if !ok || ev.Module != "HugeHardpoint1" || !ev.FullyRepaired {
					t.Errorf("mismatched AfmuRepairs: %+v", entry.Data)
				}
			},
		},
		{
			name: "Repair",
			jsonLine: `{"timestamp":"2026-10-15T12:03:00Z","event":"Repair","Item":"All","Cost":1500}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventRepair)
				if !ok || ev.Cost != 1500 {
					t.Errorf("mismatched Repair: %+v", entry.Data)
				}
			},
		},
		{
			name: "RepairAll",
			jsonLine: `{"timestamp":"2026-10-15T12:04:00Z","event":"RepairAll","Cost":4200}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventRepairAll)
				if !ok || ev.Cost != 4200 {
					t.Errorf("mismatched RepairAll: %+v", entry.Data)
				}
			},
		},
		{
			name: "RebootRepair",
			jsonLine: `{"timestamp":"2026-10-15T12:05:00Z","event":"RebootRepair","Modules":["PowerDistributor","Engine"]}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventRebootRepair)
				if !ok || len(ev.Modules) != 2 {
					t.Errorf("mismatched RebootRepair: %+v", entry.Data)
				}
			},
		},
		{
			name: "ModuleSwap",
			jsonLine: `{"timestamp":"2026-10-15T12:06:00Z","event":"ModuleSwap","FromSlot":"Slot01","ToSlot":"Slot02","FromItem":"item1","ToItem":"item2","Ship":"anaconda","ShipID":1}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventModuleSwap)
				if !ok || ev.FromSlot != "Slot01" || ev.ToSlot != "Slot02" {
					t.Errorf("mismatched ModuleSwap: %+v", entry.Data)
				}
			},
		},
		{
			name: "ModuleBuy",
			jsonLine: `{"timestamp":"2026-10-15T12:07:00Z","event":"ModuleBuy","Slot":"Slot01","BuyItem":"itemA","BuyPrice":50000,"Ship":"anaconda","ShipID":1}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventModuleBuy)
				if !ok || ev.BuyItem != "itemA" || ev.BuyPrice != 50000 {
					t.Errorf("mismatched ModuleBuy: %+v", entry.Data)
				}
			},
		},
		{
			name: "ModuleSell",
			jsonLine: `{"timestamp":"2026-10-15T12:08:00Z","event":"ModuleSell","Slot":"Slot01","SellItem":"itemA","SellPrice":45000,"Ship":"anaconda","ShipID":1}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventModuleSell)
				if !ok || ev.SellPrice != 45000 {
					t.Errorf("mismatched ModuleSell: %+v", entry.Data)
				}
			},
		},
		{
			name: "ModuleStore",
			jsonLine: `{"timestamp":"2026-10-15T12:09:00Z","event":"ModuleStore","Slot":"Slot01","StoredItem":"itemA","Ship":"anaconda","ShipID":1}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventModuleStore)
				if !ok || ev.StoredItem != "itemA" {
					t.Errorf("mismatched ModuleStore: %+v", entry.Data)
				}
			},
		},
		{
			name: "ModuleRetrieve",
			jsonLine: `{"timestamp":"2026-10-15T12:10:00Z","event":"ModuleRetrieve","Slot":"Slot01","RetrievedItem":"itemA","Ship":"anaconda","ShipID":1}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventModuleRetrieve)
				if !ok || ev.RetrievedItem != "itemA" {
					t.Errorf("mismatched ModuleRetrieve: %+v", entry.Data)
				}
			},
		},

		// Navigation / Location
		{
			name: "FSDJump",
			jsonLine: `{"timestamp":"2026-10-15T12:15:00Z","event":"FSDJump","StarSystem":"Sol","SystemAddress":10477373803,"StarPos":[0.0,0.0,0.0],"SystemAllegiance":"Federation","Population":22780871769,"JumpDist":18.5}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventFSDJump)
				if !ok || ev.StarSystem != "Sol" || ev.SystemAddress != 10477373803 || ev.JumpDist != 18.5 {
					t.Errorf("mismatched FSDJump: %+v", entry.Data)
				}
			},
		},
		{
			name: "JetConeBoost",
			jsonLine: `{"timestamp":"2026-10-16T12:00:00Z","event":"JetConeBoost","BoostValue":4.0}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventJetConeBoost)
				if !ok || ev.BoostValue != 4.0 {
					t.Errorf("mismatched JetConeBoost: %+v", entry.Data)
				}
			},
		},
		{
			name: "CarrierJump",
			jsonLine: `{"timestamp":"2026-10-15T12:16:00Z","event":"CarrierJump","StarSystem":"Colonia","SystemAddress":12345678,"StarPos":[1.0,2.0,3.0]}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventCarrierJump)
				if !ok || ev.StarSystem != "Colonia" {
					t.Errorf("mismatched CarrierJump: %+v", entry.Data)
				}
			},
		},
		{
			name: "Location",
			jsonLine: `{"timestamp":"2026-10-15T12:17:00Z","event":"Location","Docked":true,"StationName":"Jameson Memorial","StarSystem":"Shinrarta Dezhra","SystemAddress":3932277478106,"StarPos":[55.71875,17.59375,27.15625]}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventLocation)
				if !ok || !ev.Docked || ev.StationName != "Jameson Memorial" {
					t.Errorf("mismatched Location: %+v", entry.Data)
				}
			},
		},
		{
			name: "StartJump",
			jsonLine: `{"timestamp":"2026-10-15T12:18:00Z","event":"StartJump","JumpType":"Hyperspace","StarSystem":"Alpha Centauri","StarClass":"G"}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventStartJump)
				if !ok || ev.JumpType != "Hyperspace" || ev.StarClass != "G" {
					t.Errorf("mismatched StartJump: %+v", entry.Data)
				}
			},
		},

		// Exploration
		{
			name: "FSSDiscoveryScan",
			jsonLine: `{"timestamp":"2026-10-15T12:20:00Z","event":"FSSDiscoveryScan","Progress":1.0,"BodyCount":12,"SystemName":"Sol","SystemAddress":10477373803}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventFSSDiscoveryScan)
				if !ok || ev.BodyCount != 12 || ev.SystemName != "Sol" {
					t.Errorf("mismatched FSSDiscoveryScan: %+v", entry.Data)
				}
			},
		},
		{
			name: "FSSAllBodiesFound",
			jsonLine: `{"timestamp":"2026-10-15T12:21:00Z","event":"FSSAllBodiesFound","SystemName":"Sol","SystemAddress":10477373803,"Count":12}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventFSSAllBodiesFound)
				if !ok || ev.Count != 12 {
					t.Errorf("mismatched FSSAllBodiesFound: %+v", entry.Data)
				}
			},
		},
		{
			name: "Scan",
			jsonLine: `{"timestamp":"2026-10-15T12:22:00Z","event":"Scan","ScanType":"Detailed","BodyName":"Sol 3","BodyID":3,"PlanetClass":"Earthlike body","Landable":false,"WasDiscovered":true,"WasMapped":true,"WasFootfalled":true}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventScan)
				if !ok || ev.PlanetClass != "Earthlike body" || !ev.WasFootfalled {
					t.Errorf("mismatched Scan: %+v", entry.Data)
				}
			},
		},
		{
			name: "SAAScanComplete",
			jsonLine: `{"timestamp":"2026-10-15T12:23:00Z","event":"SAAScanComplete","BodyName":"Sol 3","BodyID":3,"ProbesUsed":6,"EfficiencyTarget":7}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventSAAScanComplete)
				if !ok || ev.ProbesUsed != 6 || ev.EfficiencyTarget != 7 {
					t.Errorf("mismatched SAAScanComplete: %+v", entry.Data)
				}
			},
		},
		{
			name: "FSSBodySignals",
			jsonLine: `{"timestamp":"2026-10-15T12:24:00Z","event":"FSSBodySignals","BodyName":"Sol 3 a","BodyID":4,"Signals":[{"Type":"$SAA_SignalType_Biological;","Type_Localised":"Biological","Count":3}]}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventFSSBodySignals)
				if !ok || len(ev.Signals) != 1 || ev.Signals[0].Count != 3 {
					t.Errorf("mismatched FSSBodySignals: %+v", entry.Data)
				}
			},
		},
		{
			name: "SAASignalsFound",
			jsonLine: `{"timestamp":"2026-10-15T12:25:00Z","event":"SAASignalsFound","BodyName":"Sol 3 a","BodyID":4,"Genuses":[{"Genus":"$Codex_Ent_Bacterial_Genus_Name;","Genus_Localised":"Bacterium"}]}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventSAASignalsFound)
				if !ok || len(ev.Genuses) != 1 || ev.Genuses[0].GenusLocalised != "Bacterium" {
					t.Errorf("mismatched SAASignalsFound: %+v", entry.Data)
				}
			},
		},

		// Exobiology
		{
			name: "ScanOrganic",
			jsonLine: `{"timestamp":"2026-10-15T12:30:00Z","event":"ScanOrganic","ScanType":"Log","Genus":"$Codex_Ent_Bacterial_Genus_Name;","Genus_Localised":"Bacterium","Species":"$Codex_Ent_Bacterial_01_Name;","Species_Localised":"Bacterium Aurasus","Body":4,"SystemAddress":10477373803}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventScanOrganic)
				if !ok || ev.ScanType != "Log" || ev.SpeciesLocalised != "Bacterium Aurasus" {
					t.Errorf("mismatched ScanOrganic: %+v", entry.Data)
				}
			},
		},
		{
			name: "ApproachBody",
			jsonLine: `{"timestamp":"2026-10-15T12:31:00Z","event":"ApproachBody","StarSystem":"Sol","SystemAddress":10477373803,"Body":"Sol 3 a","BodyID":4}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventApproachBody)
				if !ok || ev.Body != "Sol 3 a" {
					t.Errorf("mismatched ApproachBody: %+v", entry.Data)
				}
			},
		},
		{
			name: "LeaveBody",
			jsonLine: `{"timestamp":"2026-10-15T12:32:00Z","event":"LeaveBody","StarSystem":"Sol","SystemAddress":10477373803,"Body":"Sol 3 a","BodyID":4}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventLeaveBody)
				if !ok || ev.Body != "Sol 3 a" {
					t.Errorf("mismatched LeaveBody: %+v", entry.Data)
				}
			},
		},
		{
			name: "Touchdown",
			jsonLine: `{"timestamp":"2026-10-15T12:33:00Z","event":"Touchdown","PlayerLatitude":12.34,"PlayerLongitude":56.78,"Body":"Sol 3 a","BodyID":4,"OnPlanet":true}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventTouchdown)
				if !ok || ev.PlayerLatitude != 12.34 || !ev.OnPlanet {
					t.Errorf("mismatched Touchdown: %+v", entry.Data)
				}
			},
		},
		{
			name: "Liftoff",
			jsonLine: `{"timestamp":"2026-10-15T12:34:00Z","event":"Liftoff","PlayerLatitude":12.34,"PlayerLongitude":56.78,"Body":"Sol 3 a","BodyID":4,"OnPlanet":true}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventLiftoff)
				if !ok || !ev.OnPlanet {
					t.Errorf("mismatched Liftoff: %+v", entry.Data)
				}
			},
		},
		{
			name: "Disembark",
			jsonLine: `{"timestamp":"2026-10-15T12:35:00Z","event":"Disembark","SRV":false,"OnPlanet":true,"Body":"Sol 3 a","BodyID":4}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventDisembark)
				if !ok || !ev.OnPlanet {
					t.Errorf("mismatched Disembark: %+v", entry.Data)
				}
			},
		},
		{
			name: "SellOrganicData",
			jsonLine: `{"timestamp":"2026-10-15T12:36:00Z","event":"SellOrganicData","MarketID":128666762,"TotalEarnings":5000000,"BioData":[{"Genus":"Bacterium","Species":"Aurasus","Value":2500000,"Bonus":2500000}]}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventSellOrganicData)
				if !ok || ev.TotalEarnings != 5000000 || len(ev.BioData) != 1 {
					t.Errorf("mismatched SellOrganicData: %+v", entry.Data)
				}
			},
		},

		// General / Lifecycle
		{
			name: "LoadGame",
			jsonLine: `{"timestamp":"2026-10-15T12:40:00Z","event":"LoadGame","Commander":"Jameson","Ship":"anaconda","ShipID":1,"Credits":150000000}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventLoadGame)
				if !ok || ev.Commander != "Jameson" || ev.Credits != 150000000 {
					t.Errorf("mismatched LoadGame: %+v", entry.Data)
				}
			},
		},
		{
			name: "Fileheader",
			jsonLine: `{"timestamp":"2026-10-15T12:00:00Z","event":"Fileheader","part":1,"language":"English/UK","gameversion":"4.0.0.1800","build":"r300000"}`,
			check: func(t *testing.T, entry JournalEntry) {
				ev, ok := entry.Data.(*EventFileheader)
				if !ok || ev.Part != 1 || ev.GameVersion != "4.0.0.1800" {
					t.Errorf("mismatched Fileheader: %+v", entry.Data)
				}
			},
		},
		{
			name: "UnknownEvent",
			jsonLine: `{"timestamp":"2026-10-15T12:45:00Z","event":"CustomModEvent","SomeField":"SomeValue"}`,
			check: func(t *testing.T, entry JournalEntry) {
				if entry.Event != "CustomModEvent" {
					t.Errorf("expected Event CustomModEvent, got %q", entry.Event)
				}
				dataMap, ok := entry.Data.(map[string]any)
				if !ok || dataMap["SomeField"] != "SomeValue" {
					t.Errorf("expected map fallback with SomeField, got %+v", entry.Data)
				}
			},
		},
	}

	for _, tc := range testCases {
		t.Run(tc.name, func(t *testing.T) {
			entry, err := ParseJournalEntry([]byte(tc.jsonLine))
			if err != nil {
				t.Fatalf("ParseJournalEntry failed: %v", err)
			}
			tc.check(t, entry)
		})
	}
}

func TestJournalTailer_StartupReplayFromCheckpoint(t *testing.T) {
	tempDir := t.TempDir()
	jPath := filepath.Join(tempDir, "Journal.2026-10-15T100000.01.log")

	content := strings.Join([]string{
		`{"timestamp":"2026-10-15T10:00:00Z","event":"Fileheader","part":1}`,
		`{"timestamp":"2026-10-15T10:00:01Z","event":"Music","MusicTrack":"MainMenu"}`,
		`{"timestamp":"2026-10-15T10:01:00Z","event":"LoadGame","Commander":"CmdrOne","Ship":"cobra_mkiii"}`, // Checkpoint 1
		`{"timestamp":"2026-10-15T10:02:00Z","event":"FSDJump","StarSystem":"Sol"}`,
		`{"timestamp":"2026-10-15T10:05:00Z","event":"Location","StarSystem":"Shinrarta Dezhra","Docked":true}`, // Most recent checkpoint!
		`{"timestamp":"2026-10-15T10:06:00Z","event":"HullDamage","Health":0.95}`,
		`{"timestamp":"2026-10-15T10:07:00Z","event":"Repair","Item":"All","Cost":500}`,
	}, "\n") + "\n"

	if err := os.WriteFile(jPath, []byte(content), 0o644); err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	entriesCh, errCh := TailJournal(ctx, tempDir, WithJournalPollInterval(10*time.Millisecond))

	var received []string
	for {
		select {
		case err := <-errCh:
			t.Fatalf("unexpected tailer error: %v", err)
		case entry, ok := <-entriesCh:
			if !ok {
				goto Done
			}
			received = append(received, entry.Event)
			if len(received) == 3 {
				// We expect: Location (checkpoint), HullDamage, Repair
				goto Done
			}
		case <-ctx.Done():
			t.Fatalf("timed out waiting for replayed entries; received so far: %v", received)
		}
	}
Done:
	if len(received) != 3 {
		t.Fatalf("expected 3 replayed events, got %d: %v", len(received), received)
	}
	expected := []string{"Location", "HullDamage", "Repair"}
	for i := range expected {
		if received[i] != expected[i] {
			t.Errorf("at index %d expected %q, got %q", i, expected[i], received[i])
		}
	}
}

func TestJournalTailer_ReplayAcrossRolloverFiles(t *testing.T) {
	tempDir := t.TempDir()

	// File 1 has LoadGame checkpoint
	f1 := filepath.Join(tempDir, "Journal.2026-10-15T100000.01.log")
	content1 := strings.Join([]string{
		`{"timestamp":"2026-10-15T10:00:00Z","event":"Fileheader","part":1}`,
		`{"timestamp":"2026-10-15T10:01:00Z","event":"LoadGame","Commander":"CmdrTwo"}`,
		`{"timestamp":"2026-10-15T10:02:00Z","event":"FSDJump","StarSystem":"Sol"}`,
	}, "\n") + "\n"
	if err := os.WriteFile(f1, []byte(content1), 0o644); err != nil {
		t.Fatal(err)
	}

	// File 2 is freshly rolled, no LoadGame or Location in it yet
	f2 := filepath.Join(tempDir, "Journal.2026-10-15T100500.01.log")
	content2 := strings.Join([]string{
		`{"timestamp":"2026-10-15T10:05:00Z","event":"Fileheader","part":1}`,
		`{"timestamp":"2026-10-15T10:06:00Z","event":"HullDamage","Health":0.90}`,
	}, "\n") + "\n"
	if err := os.WriteFile(f2, []byte(content2), 0o644); err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	entriesCh, errCh := TailJournal(ctx, tempDir, WithJournalPollInterval(10*time.Millisecond))

	var received []string
	for {
		select {
		case err := <-errCh:
			t.Fatalf("unexpected error: %v", err)
		case entry, ok := <-entriesCh:
			if !ok {
				goto Done
			}
			received = append(received, entry.Event)
			if len(received) == 4 {
				// Expect LoadGame, FSDJump, Fileheader, HullDamage
				goto Done
			}
		case <-ctx.Done():
			t.Fatalf("timed out; received so far: %v", received)
		}
	}
Done:
	if len(received) != 4 {
		t.Fatalf("expected 4 events, got %d: %v", len(received), received)
	}
	expected := []string{"LoadGame", "FSDJump", "Fileheader", "HullDamage"}
	for i := range expected {
		if received[i] != expected[i] {
			t.Errorf("at index %d expected %q, got %q", i, expected[i], received[i])
		}
	}
}

func TestJournalTailer_RealtimeAppendsAndRollover(t *testing.T) {
	tempDir := t.TempDir()
	j1Path := filepath.Join(tempDir, "Journal.2026-10-15T120000.01.log")

	initial := strings.Join([]string{
		`{"timestamp":"2026-10-15T12:00:00Z","event":"Location","StarSystem":"Sol"}`,
	}, "\n") + "\n"
	if err := os.WriteFile(j1Path, []byte(initial), 0o644); err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 3*time.Second)
	defer cancel()

	entriesCh, _ := TailJournal(ctx, tempDir, WithJournalPollInterval(10*time.Millisecond))

	// 1. Receive initial replayed Location
	select {
	case entry := <-entriesCh:
		if entry.Event != "Location" {
			t.Fatalf("expected Location, got %q", entry.Event)
		}
	case <-ctx.Done():
		t.Fatal("timed out waiting for initial event")
	}

	// 2. Append new event to j1Path in real time
	f1, err := os.OpenFile(j1Path, os.O_APPEND|os.O_WRONLY, 0o644)
	if err != nil {
		t.Fatal(err)
	}
	_, _ = f1.WriteString(`{"timestamp":"2026-10-15T12:01:00Z","event":"HullDamage","Health":0.85}` + "\n")
	_ = f1.Close()

	select {
	case entry := <-entriesCh:
		if entry.Event != "HullDamage" {
			t.Fatalf("expected HullDamage, got %q", entry.Event)
		}
	case <-ctx.Done():
		t.Fatal("timed out waiting for appended event")
	}

	// 3. Trigger rollover by creating newer journal file
	j2Path := filepath.Join(tempDir, "Journal.2026-10-15T120500.01.log")
	j2Content := `{"timestamp":"2026-10-15T12:05:00Z","event":"FSDJump","StarSystem":"Proxima"}` + "\n"
	if err := os.WriteFile(j2Path, []byte(j2Content), 0o644); err != nil {
		t.Fatal(err)
	}

	select {
	case entry := <-entriesCh:
		if entry.Event != "FSDJump" {
			t.Fatalf("expected FSDJump from rolled file, got %q", entry.Event)
		}
	case <-ctx.Done():
		t.Fatal("timed out waiting for rolled file event")
	}
}

func TestSnapshotWatchers(t *testing.T) {
	tempDir := t.TempDir()

	// 1. Status.json watcher
	statusPath := filepath.Join(tempDir, "Status.json")
	statusJSON := `{
		"timestamp": "2026-10-15T14:00:00Z",
		"event": "Status",
		"Flags": 16842769,
		"Flags2": 1,
		"Pips": [4, 4, 4],
		"FireGroup": 1,
		"GuiFocus": 0,
		"Fuel": {
			"FuelMain": 31.5,
			"FuelReservoir": 0.45
		},
		"Cargo": 8.0,
		"LegalState": "Clean",
		"Latitude": 15.1234,
		"Longitude": -42.5678,
		"PlanetRadius": 6371000.0,
		"BodyName": "Earth"
	}`
	if err := os.WriteFile(statusPath, []byte(statusJSON), 0o644); err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	statusCh, _ := WatchStatus(ctx, tempDir, WithWatchPollInterval(10*time.Millisecond))
	select {
	case snap := <-statusCh:
		if !snap.IsDocked() {
			t.Errorf("expected IsDocked=true for flag 16842769")
		}
		if !snap.OnFoot() {
			t.Errorf("expected OnFoot=true for Flags2=1")
		}
		if snap.Pips.Sys() != 4 || snap.Pips.SysFloat() != 2.0 {
			t.Errorf("mismatched Pips: %+v", snap.Pips)
		}
		if snap.Fuel.FuelMain != 31.5 {
			t.Errorf("mismatched FuelMain: %v", snap.Fuel.FuelMain)
		}
		if snap.Latitude == nil || *snap.Latitude != 15.1234 {
			t.Errorf("mismatched Latitude: %v", snap.Latitude)
		}
	case <-ctx.Done():
		t.Fatal("timed out waiting for Status.json")
	}

	// 2. Cargo.json watcher
	cargoPath := filepath.Join(tempDir, "Cargo.json")
	cargoJSON := `{
		"timestamp": "2026-10-15T14:01:00Z",
		"event": "Cargo",
		"Vessel": "Ship",
		"Count": 10,
		"CargoCapacity": 64,
		"Inventory": [
			{"Name": "gold", "Name_Localised": "Gold", "Count": 6, "Stolen": 0},
			{"Name": "silver", "Name_Localised": "Silver", "Count": 4, "Stolen": 1}
		]
	}`
	if err := os.WriteFile(cargoPath, []byte(cargoJSON), 0o644); err != nil {
		t.Fatal(err)
	}

	cargoCh, _ := WatchCargo(ctx, tempDir, WithWatchPollInterval(10*time.Millisecond))
	select {
	case snap := <-cargoCh:
		if snap.Count != 10 || snap.TotalInventoryCount() != 10 {
			t.Errorf("mismatched cargo count: %d", snap.Count)
		}
		gold := snap.FindItem("Gold")
		if gold == nil || gold.Count != 6 {
			t.Errorf("expected 6 gold, got %+v", gold)
		}
	case <-ctx.Done():
		t.Fatal("timed out waiting for Cargo.json")
	}

	// 3. Market.json watcher
	marketPath := filepath.Join(tempDir, "Market.json")
	marketJSON := `{
		"timestamp": "2026-10-15T14:02:00Z",
		"event": "Market",
		"MarketID": 128666762,
		"StationName": "Jameson Memorial",
		"StarSystem": "Shinrarta Dezhra",
		"Items": [
			{
				"id": 128049152,
				"Name": "$powergenerators_name;",
				"Name_Localised": "Power Generators",
				"Category": "$MARKET_category_machinery;",
				"BuyPrice": 450,
				"SellPrice": 425,
				"MeanPrice": 580
			}
		]
	}`
	if err := os.WriteFile(marketPath, []byte(marketJSON), 0o644); err != nil {
		t.Fatal(err)
	}

	marketCh, _ := WatchMarket(ctx, tempDir, WithWatchPollInterval(10*time.Millisecond))
	select {
	case snap := <-marketCh:
		price, ok := snap.MeanPrice("Power Generators")
		if !ok || price != 580 {
			t.Errorf("expected MeanPrice 580 by localized name, got %d, %v", price, ok)
		}
		price2, ok2 := snap.MeanPrice("powergenerators")
		if !ok2 || price2 != 580 {
			t.Errorf("expected MeanPrice 580 by cleaned symbol name, got %d, %v", price2, ok2)
		}
	case <-ctx.Done():
		t.Fatal("timed out waiting for Market.json")
	}

	// 4. NavRoute.json watcher
	navPath := filepath.Join(tempDir, "NavRoute.json")
	navJSON := `{
		"timestamp": "2026-10-15T14:03:00Z",
		"event": "NavRoute",
		"Route": [
			{"StarSystem": "Sol", "SystemAddress": 1, "StarPos": [0.0, 0.0, 0.0], "StarClass": "G"},
			{"StarSystem": "Alpha Centauri", "SystemAddress": 2, "StarPos": [0.0, 0.0, 4.3], "StarClass": "G"},
			{"StarSystem": "Barnard's Star", "SystemAddress": 3, "StarPos": [0.0, 0.0, 10.3], "StarClass": "M"}
		]
	}`
	if err := os.WriteFile(navPath, []byte(navJSON), 0o644); err != nil {
		t.Fatal(err)
	}

	navCh, _ := WatchNavRoute(ctx, tempDir, WithWatchPollInterval(10*time.Millisecond))
	select {
	case snap := <-navCh:
		if len(snap.Route) != 3 {
			t.Fatalf("expected 3 route entries, got %d", len(snap.Route))
		}
		dist := snap.TotalDistance()
		if dist < 10.2 || dist > 10.4 {
			t.Errorf("expected approx 10.3 total distance, got %f", dist)
		}
		next := snap.NextHop("Alpha Centauri")
		if next == nil || next.StarSystem != "Barnard's Star" {
			t.Errorf("expected next hop Barnard's Star, got %+v", next)
		}
		remaining := snap.RemainingJumps("Alpha Centauri")
		if remaining != 1 {
			t.Errorf("expected 1 remaining jump, got %d", remaining)
		}
	case <-ctx.Done():
		t.Fatal("timed out waiting for NavRoute.json")
	}

	// 5. ModuleInfo.json watcher
	modPath := filepath.Join(tempDir, "ModuleInfo.json")
	modJSON := `{
		"timestamp": "2026-10-15T14:04:00Z",
		"event": "ModuleInfo",
		"Modules": [
			{"Slot": "HugeHardpoint1", "Item": "hpt_multicannon_gimbal_huge", "Power": 2.14, "Priority": 1},
			{"Slot": "MediumHardpoint1", "Item": "hpt_beamlaser_gimbal_medium", "Power": 1.25, "Priority": 2}
		]
	}`
	if err := os.WriteFile(modPath, []byte(modJSON), 0o644); err != nil {
		t.Fatal(err)
	}

	modCh, _ := WatchModuleInfo(ctx, tempDir, WithWatchPollInterval(10*time.Millisecond))
	select {
	case snap := <-modCh:
		entry := snap.FindSlot("hugehardpoint1")
		if entry == nil || entry.Power != 2.14 || entry.Priority != 1 {
			t.Errorf("mismatched slot lookup: %+v", entry)
		}
	case <-ctx.Done():
		t.Fatal("timed out waiting for ModuleInfo.json")
	}
}

func TestWatcher_WriteRaceRetryRecovery(t *testing.T) {
	tempDir := t.TempDir()
	statusPath := filepath.Join(tempDir, "Status.json")

	// Initially write truncated / invalid JSON (simulating partial write during game save)
	if err := os.WriteFile(statusPath, []byte(`{"timestamp": "2026-10-15T15:00:00Z", "event": "Status", "Flags": 123, `), 0o644); err != nil {
		t.Fatal(err)
	}

	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	statusCh, _ := WatchStatus(ctx, tempDir,
		WithWatchPollInterval(10*time.Millisecond),
		WithWatchRetryDelay(10*time.Millisecond),
		WithWatchMaxRetries(5),
	)

	// In the background, write the complete valid JSON 30ms later
	go func() {
		time.Sleep(30 * time.Millisecond)
		validJSON := `{"timestamp": "2026-10-15T15:00:00Z", "event": "Status", "Flags": 12345, "Flags2": 0, "Pips": [4,4,4]}`
		_ = os.WriteFile(statusPath, []byte(validJSON), 0o644)
	}()

	select {
	case snap := <-statusCh:
		if snap.Flags != 12345 {
			t.Errorf("expected recovered Flags=12345, got %d", snap.Flags)
		}
	case <-ctx.Done():
		t.Fatal("watcher failed to recover from partial write race")
	}
}

func TestDiscovery_LegacyJournalOrdering(t *testing.T) {
	tempDir := t.TempDir()

	// Legacy 2022 journals: Journal.YYMMDDHHMMSS.01.log
	legacy1 := filepath.Join(tempDir, "Journal.220512101952.01.log")
	legacy2 := filepath.Join(tempDir, "Journal.221117191657.01.log")

	// Modern 2026 journals: Journal.YYYY-MM-DDTHHMMSS.01.log
	modern1 := filepath.Join(tempDir, "Journal.2026-10-15T224255.01.log")
	modern2 := filepath.Join(tempDir, "Journal.2026-10-16T001949.01.log")

	// Write files
	if err := os.WriteFile(legacy1, []byte("{}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(legacy2, []byte("{}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(modern1, []byte("{}\n"), 0o644); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(modern2, []byte("{}\n"), 0o644); err != nil {
		t.Fatal(err)
	}

	// Set modification times reflecting their real creation dates
	tLegacy1 := time.Date(2022, time.May, 13, 17, 58, 31, 0, time.UTC)
	tLegacy2 := time.Date(2022, time.November, 18, 9, 0, 56, 0, time.UTC)
	tModern1 := time.Date(2026, time.October, 15, 23, 3, 18, 0, time.UTC)
	tModern2 := time.Date(2026, time.October, 16, 0, 50, 5, 0, time.UTC)

	_ = os.Chtimes(legacy1, tLegacy1, tLegacy1)
	_ = os.Chtimes(legacy2, tLegacy2, tLegacy2)
	_ = os.Chtimes(modern1, tModern1, tModern1)
	_ = os.Chtimes(modern2, tModern2, tModern2)

	files, err := ListJournalFiles(tempDir)
	if err != nil {
		t.Fatalf("ListJournalFiles failed: %v", err)
	}
	if len(files) != 4 {
		t.Fatalf("expected 4 files, got %d", len(files))
	}

	// Modern journal must be latest, legacy journals must come first
	if files[0] != legacy1 || files[1] != legacy2 || files[2] != modern1 || files[3] != modern2 {
		t.Fatalf("unexpected chronological order: %v", files)
	}

	latest, err := FindLatestJournal(tempDir)
	if err != nil {
		t.Fatalf("FindLatestJournal failed: %v", err)
	}
	if latest != modern2 {
		t.Fatalf("expected latest journal to be %q, got %q", modern2, latest)
	}
}

func TestWatchModuleInfo_PluralAndFallback(t *testing.T) {
	tempDir := t.TempDir()
	pluralPath := filepath.Join(tempDir, "ModulesInfo.json")

	modulesJSON := `{
		"timestamp": "2026-10-16T05:46:00Z",
		"event": "ModuleInfo",
		"Modules": [
			{"Slot": "HugeHardpoint1", "Item": "hpt_multicannon_gimbal_huge", "Power": 1.22, "Priority": 1},
			{"Slot": "MainEngines", "Item": "int_engine_size7_class5", "Power": 10.21, "Priority": 0}
		]
	}`
	if err := os.WriteFile(pluralPath, []byte(modulesJSON), 0o644); err != nil {
		t.Fatal(err)
	}

	// Test resolveSnapshotPath on directory with ModulesInfo.json
	resolved := resolveSnapshotPath(tempDir, "ModuleInfo.json")
	if resolved != pluralPath {
		t.Fatalf("expected resolveSnapshotPath to resolve to %q, got %q", pluralPath, resolved)
	}

	// Test ReadModuleInfo on singular path falls back to plural
	singularPath := filepath.Join(tempDir, "ModuleInfo.json")
	snap, err := ReadModuleInfo(singularPath)
	if err != nil {
		t.Fatalf("ReadModuleInfo fallback failed: %v", err)
	}
	if len(snap.Modules) != 2 {
		t.Fatalf("expected 2 modules, got %d", len(snap.Modules))
	}

	// Test WatchModuleInfo watching tempDir picks up ModulesInfo.json
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	modCh, _ := WatchModuleInfo(ctx, tempDir, WithWatchPollInterval(10*time.Millisecond))
	select {
	case s := <-modCh:
		entry := s.FindSlot("hugehardpoint1")
		if entry == nil || entry.Power != 1.22 {
			t.Fatalf("unexpected entry: %+v", entry)
		}
	case <-ctx.Done():
		t.Fatal("timed out waiting for ModulesInfo.json watcher")
	}
}

func TestRealSavedGamesFolder(t *testing.T) {
	candidateDirs := []string{
		`C:\Users\csasn\Saved Games\Frontier Developments\Elite Dangerous`,
		`/mnt/c/Users/csasn/Saved Games/Frontier Developments/Elite Dangerous`,
	}

	var savedGamesDir string
	for _, d := range candidateDirs {
		if fi, err := os.Stat(d); err == nil && fi.IsDir() {
			savedGamesDir = d
			break
		}
	}

	if savedGamesDir == "" {
		t.Skip("skipping real saved games test: directory not found")
	}

	// 1. Verify FindLatestJournal finds Journal.2026-10-16T001949.01.log
	latest, err := FindLatestJournal(savedGamesDir)
	if err != nil {
		t.Fatalf("FindLatestJournal on real folder failed: %v", err)
	}
	base := filepath.Base(latest)
	if base != "Journal.2026-10-16T001949.01.log" {
		t.Errorf("expected latest journal Journal.2026-10-16T001949.01.log, got %s", base)
	}

	// 2. Verify ModulesInfo.json can be read and parsed
	modPath := filepath.Join(savedGamesDir, "ModulesInfo.json")
	snap, err := ReadModuleInfo(modPath)
	if err != nil {
		t.Fatalf("ReadModuleInfo on real ModulesInfo.json failed: %v", err)
	}
	if len(snap.Modules) == 0 {
		t.Fatalf("expected non-empty modules from real ModulesInfo.json")
	}
	t.Logf("Successfully parsed real ModulesInfo.json: %d modules found (timestamp %v)", len(snap.Modules), snap.Timestamp)

	// 3. Verify WatchModuleInfo works on the real Saved Games directory
	ctx, cancel := context.WithTimeout(context.Background(), 2*time.Second)
	defer cancel()

	modCh, errCh := WatchModuleInfo(ctx, savedGamesDir, WithWatchPollInterval(10*time.Millisecond))
	select {
	case snap := <-modCh:
		if len(snap.Modules) == 0 {
			t.Errorf("WatchModuleInfo emitted empty modules")
		}
		t.Logf("WatchModuleInfo successfully emitted snapshot with %d modules", len(snap.Modules))
	case err := <-errCh:
		t.Fatalf("WatchModuleInfo emitted error: %v", err)
	case <-ctx.Done():
		t.Fatalf("WatchModuleInfo timed out on real Saved Games folder")
	}
}
