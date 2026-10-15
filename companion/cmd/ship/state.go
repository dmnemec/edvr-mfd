package main

import (
	"fmt"
	"math"
	"sort"
	"strings"
	"sync"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
)

// ShipModule represents an installed module with telemetry and display attributes.
type ShipModule struct {
	Slot        string
	Item        string
	On          bool
	Priority    int
	Health      float64
	PowerDraw   float64
	Engineering *elite.ModuleEngineering

	Label    string
	Sublabel string
	Group    int
}

// ShipState tracks the aggregate ship state across Journal events and snapshot files.
type ShipState struct {
	mu sync.RWMutex

	// Loadout & Hull
	Ship          string
	ShipName      string
	ShipIdent     string
	HullHealth    float64
	CargoCapacity int
	MaxJumpRange  float64
	FuelCapacity  elite.FuelCapacityInfo
	Modules       []ShipModule

	// Status.json
	Pips        elite.Pips
	Fuel        elite.StatusFuel
	Overheating bool
	LowFuel     bool

	// Cargo.json
	Cargo *elite.CargoSnapshot

	// ModuleInfo.json
	ModuleInfo *elite.ModuleInfoSnapshot

	// Power Plant capacity
	PowerPlantCapacity float64

	// MeanPrice cache
	PriceCache *PriceCache

	// Integrity tracking
	lastIntegrityHash string
	hasLoadout        bool
}

// NewShipState creates an initialized ShipState with the provided price cache.
func NewShipState(pc *PriceCache) *ShipState {
	return &ShipState{
		HullHealth: 1.0,
		Pips:       elite.Pips{4, 4, 4},
		PriceCache: pc,
	}
}

// ComputeIntegrityHash computes a signature of module health, On status, and hull health (§12.1).
func (s *ShipState) ComputeIntegrityHash() string {
	var sb strings.Builder
	sb.WriteString(fmt.Sprintf("hull:%.4f|count:%d|", s.HullHealth, len(s.Modules)))
	for _, m := range s.Modules {
		sb.WriteString(fmt.Sprintf("%s:%.4f:%t|", m.Slot, m.Health, m.On))
	}
	return sb.String()
}

// HandleJournalEntry updates state from Journal events.
// Returns which screens were affected and require disk writes.
func (s *ShipState) HandleJournalEntry(entry elite.JournalEntry) (modulesChanged, powerChanged, cargoChanged bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	switch ev := entry.Data.(type) {
	case *elite.EventLoadout:
		s.hasLoadout = true
		s.Ship = ev.Ship
		s.ShipName = ev.ShipName
		s.ShipIdent = ev.ShipIdent
		s.HullHealth = ev.HullHealth
		if ev.CargoCapacity > 0 {
			s.CargoCapacity = ev.CargoCapacity
		}
		s.MaxJumpRange = ev.MaxJumpRange
		s.FuelCapacity = ev.FuelCapacity

		modules := make([]ShipModule, 0, len(ev.Modules))
		var ppItem string
		var ppEng *elite.ModuleEngineering

		for _, m := range ev.Modules {
			mod := ShipModule{
				Slot:        m.Slot,
				Item:        m.Item,
				On:          m.On,
				Priority:    m.Priority,
				Health:      m.Health,
				Engineering: m.Engineering,
				Label:       FormatModuleName(m.Item),
				Sublabel:    FormatModuleSublabel(m.Slot, m.Item),
				Group:       SlotGroup(m.Slot),
			}

			// Attach PowerDraw if ModuleInfo was previously loaded
			if s.ModuleInfo != nil {
				if info := s.ModuleInfo.FindSlot(m.Slot); info != nil {
					mod.PowerDraw = info.Power
					if info.Priority > 0 {
						mod.Priority = info.Priority
					}
				}
			}

			if strings.EqualFold(m.Slot, "PowerPlant") || strings.EqualFold(m.Slot, "Core0") {
				ppItem = m.Item
				ppEng = m.Engineering
			}

			modules = append(modules, mod)
		}
		s.Modules = modules

		if ppItem != "" {
			s.PowerPlantCapacity = GetPowerPlantCapacity(ppItem, ppEng)
		} else if s.PowerPlantCapacity <= 0 {
			s.PowerPlantCapacity = 15.0
		}

		currentHash := s.ComputeIntegrityHash()
		if currentHash != s.lastIntegrityHash {
			s.lastIntegrityHash = currentHash
			modulesChanged = true
		}

		powerChanged = true
		cargoChanged = true

	case *elite.EventHullDamage:
		if ev.Health != s.HullHealth {
			s.HullHealth = ev.Health
			currentHash := s.ComputeIntegrityHash()
			if currentHash != s.lastIntegrityHash {
				s.lastIntegrityHash = currentHash
				modulesChanged = true
			}
		}

	case *elite.EventAfmuRepairs:
		for i := range s.Modules {
			if strings.EqualFold(s.Modules[i].Slot, ev.Module) || strings.EqualFold(s.Modules[i].Item, ev.Module) {
				if ev.FullyRepaired {
					s.Modules[i].Health = 1.0
				} else {
					s.Modules[i].Health = ev.Health
				}
				break
			}
		}
		currentHash := s.ComputeIntegrityHash()
		if currentHash != s.lastIntegrityHash {
			s.lastIntegrityHash = currentHash
			modulesChanged = true
		}

	case *elite.EventRepair:
		if strings.EqualFold(ev.Item, "All") {
			s.HullHealth = 1.0
			for i := range s.Modules {
				s.Modules[i].Health = 1.0
			}
		} else {
			for i := range s.Modules {
				if strings.EqualFold(s.Modules[i].Slot, ev.Item) || strings.EqualFold(s.Modules[i].Item, ev.Item) {
					s.Modules[i].Health = 1.0
					break
				}
			}
		}
		currentHash := s.ComputeIntegrityHash()
		if currentHash != s.lastIntegrityHash {
			s.lastIntegrityHash = currentHash
			modulesChanged = true
		}

	case *elite.EventRepairAll:
		s.HullHealth = 1.0
		for i := range s.Modules {
			s.Modules[i].Health = 1.0
		}
		currentHash := s.ComputeIntegrityHash()
		if currentHash != s.lastIntegrityHash {
			s.lastIntegrityHash = currentHash
			modulesChanged = true
		}

	case *elite.EventRebootRepair:
		for _, modName := range ev.Modules {
			for i := range s.Modules {
				if strings.EqualFold(s.Modules[i].Slot, modName) || strings.EqualFold(s.Modules[i].Item, modName) {
					if s.Modules[i].Health < 0.05 {
						s.Modules[i].Health = 0.05
					}
					break
				}
			}
		}
		currentHash := s.ComputeIntegrityHash()
		if currentHash != s.lastIntegrityHash {
			s.lastIntegrityHash = currentHash
			modulesChanged = true
		}
	}

	return modulesChanged, powerChanged, cargoChanged
}

// HandleStatus updates telemetry from Status.json.
func (s *ShipState) HandleStatus(snap elite.StatusSnapshot) bool {
	s.mu.Lock()
	defer s.mu.Unlock()

	pipsChanged := s.Pips != snap.Pips
	fuelChanged := s.Fuel != snap.Fuel
	overheatingChanged := s.Overheating != snap.Overheating()
	lowFuelChanged := s.LowFuel != snap.LowFuel()

	s.Pips = snap.Pips
	s.Fuel = snap.Fuel
	s.Overheating = snap.Overheating()
	s.LowFuel = snap.LowFuel()

	return pipsChanged || fuelChanged || overheatingChanged || lowFuelChanged
}

// HandleCargo updates inventory from Cargo.json.
func (s *ShipState) HandleCargo(snap elite.CargoSnapshot) bool {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.Cargo = &snap
	if snap.CargoCapacity > 0 {
		s.CargoCapacity = snap.CargoCapacity
	}
	return true
}

// HandleMarket updates commodity prices from Market.json.
func (s *ShipState) HandleMarket(snap elite.MarketSnapshot) bool {
	if s.PriceCache == nil {
		return false
	}
	return s.PriceCache.UpdateFromMarket(&snap)
}

// HandleModuleInfo updates power draw and priority from ModuleInfo.json.
func (s *ShipState) HandleModuleInfo(snap elite.ModuleInfoSnapshot) bool {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.ModuleInfo = &snap
	changed := false

	for i := range s.Modules {
		if entry := snap.FindSlot(s.Modules[i].Slot); entry != nil {
			if s.Modules[i].PowerDraw != entry.Power || s.Modules[i].Priority != entry.Priority {
				s.Modules[i].PowerDraw = entry.Power
				if entry.Priority > 0 {
					s.Modules[i].Priority = entry.Priority
				}
				changed = true
			}
		}
	}

	return changed
}

// BuildShipDisplay generates the consolidated display model for screens/ship.json (Ticket MFD-008).
// Consolidates Integrity (modules list), Power (distribution bars), and Cargo (inventory list).
func (s *ShipState) BuildShipDisplay() *display.Display {
	s.mu.RLock()
	defer s.mu.RUnlock()

	// 1. Integrity tab & module analysis
	damagedCount := 0
	minHealth := 1.0

	modules := make([]ShipModule, len(s.Modules))
	copy(modules, s.Modules)

	for _, m := range modules {
		if m.Health < 0.9999 {
			damagedCount++
			if m.Health < minHealth {
				minHealth = m.Health
			}
		}
	}

	sort.SliceStable(modules, func(i, j int) bool {
		mI, mJ := modules[i], modules[j]
		iDamaged := mI.Health < 0.9999
		jDamaged := mJ.Health < 0.9999

		if iDamaged && !jDamaged {
			return true
		}
		if !iDamaged && jDamaged {
			return false
		}
		if iDamaged && jDamaged {
			if math.Abs(mI.Health-mJ.Health) > 0.001 {
				return mI.Health < mJ.Health
			}
		}

		if mI.Group != mJ.Group {
			return mI.Group < mJ.Group
		}
		return mI.Slot < mJ.Slot
	})

	integrityTab := display.Tab{
		ID:    "integrity",
		Title: "INTEGRITY",
		Type:  display.TabTypeList,
	}

	if len(modules) == 0 {
		integrityTab.Items = []display.Item{
			{
				ID:         "placeholder",
				Label:      "No module telemetry",
				Value:      "—",
				ValueColor: display.PaletteDim,
			},
		}
	} else {
		for _, m := range modules {
			pct := int(math.Round(m.Health * 100))
			valColor := display.PaletteOK
			if m.Health < 0.40 {
				valColor = display.PaletteCrit
			} else if m.Health < 0.80 {
				valColor = display.PaletteWarn
			}

			icon := ""
			if m.Health < 0.80 || !m.On {
				icon = display.IconWarning
			}

			item := display.Item{
				ID:         "slot_" + strings.ToLower(m.Slot),
				Label:      m.Label,
				Sublabel:   m.Sublabel,
				Value:      fmt.Sprintf("%d%%", pct),
				ValueColor: valColor,
				Icon:       icon,
				Action:     display.ActionEvent,
			}
			integrityTab.Items = append(integrityTab.Items, item)
		}
	}

	// 2. Power tab (bars)
	capacity := s.PowerPlantCapacity
	if capacity <= 0 {
		capacity = 15.0
	}

	totalDraw := 0.0
	for _, m := range s.Modules {
		if m.On {
			totalDraw += m.PowerDraw
		}
	}

	fuelMain := s.Fuel.FuelMain
	fuelCapMain := s.FuelCapacity.Main
	if fuelCapMain <= 0 {
		fuelCapMain = math.Max(fuelMain, 32.0)
	}

	fuelRes := s.Fuel.FuelReservoir
	fuelCapRes := s.FuelCapacity.Reserve
	if fuelCapRes <= 0 {
		fuelCapRes = math.Max(fuelRes, 1.0)
	}

	powerBarColor := display.PaletteOK
	if totalDraw > capacity {
		powerBarColor = display.PaletteCrit
	} else if totalDraw > 0.90*capacity {
		powerBarColor = display.PaletteWarn
	}

	fuelBarColor := display.PaletteOK
	if s.LowFuel {
		fuelBarColor = display.PaletteCrit
	}

	powerTab := display.Tab{
		ID:    "power",
		Title: "POWER",
		Type:  display.TabTypeBars,
		Bars: []display.Bar{
			{
				ID:       "sys",
				Label:    "SYS",
				Value:    float64(s.Pips.Sys()),
				Max:      8,
				Segments: 8,
				Color:    display.PaletteAccent,
			},
			{
				ID:       "eng",
				Label:    "ENG",
				Value:    float64(s.Pips.Eng()),
				Max:      8,
				Segments: 8,
				Color:    display.PaletteAccent,
			},
			{
				ID:       "wep",
				Label:    "WEP",
				Value:    float64(s.Pips.Wep()),
				Max:      8,
				Segments: 8,
				Color:    display.PaletteAccent,
			},
			{
				ID:    "power_draw",
				Label: fmt.Sprintf("Power Draw (%.1f / %.1f MW)", totalDraw, capacity),
				Value: totalDraw,
				Max:   capacity,
				Color: powerBarColor,
			},
			{
				ID:    "fuel_main",
				Label: fmt.Sprintf("Main Fuel (%.1f / %.1f T)", fuelMain, fuelCapMain),
				Value: fuelMain,
				Max:   fuelCapMain,
				Color: fuelBarColor,
			},
			{
				ID:    "fuel_res",
				Label: fmt.Sprintf("Reservoir (%.2f / %.2f T)", fuelRes, fuelCapRes),
				Value: fuelRes,
				Max:   fuelCapRes,
				Color: display.PaletteOK,
			},
		},
	}

	// 3. Cargo tab (list)
	var inventory []elite.CargoItem
	if s.Cargo != nil {
		inventory = s.Cargo.Inventory
	}

	type cargoRow struct {
		item      elite.CargoItem
		meanPrice int64
		hasPrice  bool
	}

	rows := make([]cargoRow, 0, len(inventory))
	for _, it := range inventory {
		price, ok := s.lookupPrice(it.Name, it.NameLocalised)
		rows = append(rows, cargoRow{
			item:      it,
			meanPrice: price,
			hasPrice:  ok && price > 0,
		})
	}

	cargoTab := display.Tab{
		ID:    "cargo",
		Title: "CARGO",
		Type:  display.TabTypeList,
	}

	if len(rows) == 0 {
		cargoTab.Items = []display.Item{
			{
				ID:         "placeholder",
				Label:      "No cargo detected",
				Value:      "—",
				ValueColor: display.PaletteDim,
			},
		}
	} else {
		for i, r := range rows {
			label := r.item.NameLocalised
			if label == "" {
				label = FormatModuleName(r.item.Name)
			}

			sub := fmt.Sprintf("%d T", r.item.Count)
			if r.item.Stolen > 0 {
				sub = fmt.Sprintf("%d T (%d Stolen)", r.item.Count, r.item.Stolen)
			}

			valStr := "—"
			valColor := display.PaletteDim
			if r.hasPrice {
				valStr = formatCredits(r.meanPrice) + " CR"
				valColor = display.PaletteNormal
			}

			icon := ""
			if r.item.Stolen > 0 {
				icon = display.IconLock
			}

			clean := elite.CleanItemName(r.item.Name)
			if clean == "" {
				clean = fmt.Sprintf("item_%d", i)
			}

			item := display.Item{
				ID:         fmt.Sprintf("cargo_%s_%d", clean, i),
				Label:      label,
				Sublabel:   sub,
				Value:      valStr,
				ValueColor: valColor,
				Icon:       icon,
				Action:     display.ActionEvent,
			}
			cargoTab.Items = append(cargoTab.Items, item)
		}
	}

	// Display metadata & StatusBadge
	badgeText := "NOMINAL"
	badgeColor := display.PaletteOK
	if s.Overheating {
		badgeText = "OVERHEATING"
		badgeColor = display.PaletteCrit
	} else if s.LowFuel {
		badgeText = "LOW FUEL"
		badgeColor = display.PaletteCrit
	} else if damagedCount > 0 {
		badgeText = fmt.Sprintf("%d DAMAGED", damagedCount)
		if minHealth < 0.40 {
			badgeColor = display.PaletteCrit
		} else {
			badgeColor = display.PaletteWarn
		}
	}

	subtitle := formatShipName(s.Ship)
	if subtitle == "" {
		subtitle = "STANDBY"
	}

	return &display.Display{
		SchemaVersion: display.CurrentSchemaVersion,
		Title:         "SHIP READOUT",
		Subtitle:      subtitle,
		StatusBadge: &display.StatusBadge{
			Text:  badgeText,
			Color: badgeColor,
		},
		FooterHint: "UP/DOWN: SCROLL | LEFT/RIGHT: TAB",
		Tabs:       []display.Tab{integrityTab, powerTab, cargoTab},
	}
}

// BuildModulesDisplay generates the display model for screens/modules.json (§12.1).
func (s *ShipState) BuildModulesDisplay() *display.Display {
	s.mu.RLock()
	defer s.mu.RUnlock()

	d := &display.Display{
		SchemaVersion: display.CurrentSchemaVersion,
		Title:         "MODULES",
		Subtitle:      "INTEGRITY",
		FooterHint:    "SELECT: DETAILS",
	}

	// Calculate damaged count
	damagedCount := 0
	minHealth := 1.0

	// Make a copy of modules for sorting
	modules := make([]ShipModule, len(s.Modules))
	copy(modules, s.Modules)

	for _, m := range modules {
		if m.Health < 0.9999 {
			damagedCount++
			if m.Health < minHealth {
				minHealth = m.Health
			}
		}
	}

	// Status badge
	if damagedCount == 0 {
		d.StatusBadge = &display.StatusBadge{
			Text:  "NOMINAL",
			Color: display.PaletteOK,
		}
	} else {
		badgeColor := display.PaletteWarn
		if minHealth < 0.40 {
			badgeColor = display.PaletteCrit
		}
		d.StatusBadge = &display.StatusBadge{
			Text:  fmt.Sprintf("%d DAMAGED", damagedCount),
			Color: badgeColor,
		}
	}

	// Sorting per §12.1:
	// "damaged (health < 100) first ascending, then by slot group (core -> optional -> hardpoints -> utility)"
	sort.SliceStable(modules, func(i, j int) bool {
		mI, mJ := modules[i], modules[j]
		iDamaged := mI.Health < 0.9999
		jDamaged := mJ.Health < 0.9999

		if iDamaged && !jDamaged {
			return true
		}
		if !iDamaged && jDamaged {
			return false
		}
		if iDamaged && jDamaged {
			if math.Abs(mI.Health-mJ.Health) > 0.001 {
				return mI.Health < mJ.Health
			}
		}

		if mI.Group != mJ.Group {
			return mI.Group < mJ.Group
		}
		return mI.Slot < mJ.Slot
	})

	// Tab 1: Integrity (list)
	integrityTab := display.Tab{
		ID:    "integrity",
		Title: "INTEGRITY",
		Type:  display.TabTypeList,
	}

	if len(modules) == 0 {
		integrityTab.Items = []display.Item{
			{
				ID:         "placeholder",
				Label:      "No module telemetry",
				Value:      "—",
				ValueColor: display.PaletteDim,
			},
		}
	} else {
		for _, m := range modules {
			pct := int(math.Round(m.Health * 100))
			valColor := display.PaletteOK
			if m.Health < 0.40 {
				valColor = display.PaletteCrit
			} else if m.Health < 0.80 {
				valColor = display.PaletteWarn
			}

			icon := ""
			if m.Health < 0.80 || !m.On {
				icon = display.IconWarning
			}

			item := display.Item{
				ID:         "slot_" + strings.ToLower(m.Slot),
				Label:      m.Label,
				Sublabel:   m.Sublabel,
				Value:      fmt.Sprintf("%d%%", pct),
				ValueColor: valColor,
				Icon:       icon,
				Action:     display.ActionEvent,
			}
			integrityTab.Items = append(integrityTab.Items, item)
		}
	}

	// Tab 2: Hull (keyvalue)
	hullPct := int(math.Round(s.HullHealth * 100))
	hullColor := display.PaletteOK
	if s.HullHealth < 0.40 {
		hullColor = display.PaletteCrit
	} else if s.HullHealth < 0.80 {
		hullColor = display.PaletteWarn
	}

	damagedColor := display.PaletteOK
	if damagedCount > 0 {
		damagedColor = display.PaletteWarn
	}

	shipModel := formatShipName(s.Ship)
	if shipModel == "" {
		shipModel = "—"
	}

	hullTab := display.Tab{
		ID:    "hull",
		Title: "HULL",
		Type:  display.TabTypeKeyValue,
		KeyValues: []display.KeyValue{
			{
				ID:         "hull_pct",
				Key:        "Hull Integrity",
				Value:      fmt.Sprintf("%d%%", hullPct),
				ValueColor: hullColor,
			},
			{
				ID:         "damaged_count",
				Key:        "Damaged Modules",
				Value:      fmt.Sprintf("%d", damagedCount),
				ValueColor: damagedColor,
			},
			{
				ID:         "ship_model",
				Key:        "Ship Model",
				Value:      shipModel,
				ValueColor: display.PaletteNormal,
			},
			{
				ID:         "ship_ident",
				Key:        "Ship Ident",
				Value:      defaultStr(s.ShipIdent, "—"),
				ValueColor: display.PaletteNormal,
			},
			{
				ID:         "ship_name",
				Key:        "Ship Name",
				Value:      defaultStr(s.ShipName, "—"),
				ValueColor: display.PaletteNormal,
			},
		},
	}

	d.Tabs = []display.Tab{integrityTab, hullTab}
	return d
}

// BuildPowerDisplay generates the display model for screens/power.json (§12.1).
func (s *ShipState) BuildPowerDisplay() *display.Display {
	s.mu.RLock()
	defer s.mu.RUnlock()

	d := &display.Display{
		SchemaVersion: display.CurrentSchemaVersion,
		Title:         "POWER",
		Subtitle:      "DISTRIBUTION & DRAW",
		FooterHint:    "SELECT: DETAILS",
	}

	// Status badge
	if s.Overheating {
		d.StatusBadge = &display.StatusBadge{
			Text:  "OVERHEATING",
			Color: display.PaletteCrit,
		}
	} else if s.LowFuel {
		d.StatusBadge = &display.StatusBadge{
			Text:  "LOW FUEL",
			Color: display.PaletteCrit,
		}
	} else {
		d.StatusBadge = &display.StatusBadge{
			Text:  "NOMINAL",
			Color: display.PaletteOK,
		}
	}

	// Capacity & Draw
	capacity := s.PowerPlantCapacity
	if capacity <= 0 {
		capacity = 15.0
	}

	totalDraw := 0.0
	for _, m := range s.Modules {
		if m.On {
			totalDraw += m.PowerDraw
		}
	}

	// Fuel values
	fuelMain := s.Fuel.FuelMain
	fuelCapMain := s.FuelCapacity.Main
	if fuelCapMain <= 0 {
		fuelCapMain = math.Max(fuelMain, 32.0)
	}

	fuelRes := s.Fuel.FuelReservoir
	fuelCapRes := s.FuelCapacity.Reserve
	if fuelCapRes <= 0 {
		fuelCapRes = math.Max(fuelRes, 1.0)
	}

	// Tab 1: Pips & Gauges (bars)
	powerBarColor := display.PaletteOK
	if totalDraw > capacity {
		powerBarColor = display.PaletteCrit
	} else if totalDraw > 0.90*capacity {
		powerBarColor = display.PaletteWarn
	}

	fuelBarColor := display.PaletteOK
	if s.LowFuel {
		fuelBarColor = display.PaletteCrit
	}

	pipsTab := display.Tab{
		ID:    "pips",
		Title: "PIPS & POWER",
		Type:  display.TabTypeBars,
		Bars: []display.Bar{
			{
				ID:       "sys",
				Label:    "SYS",
				Value:    float64(s.Pips.Sys()),
				Max:      8,
				Segments: 8,
				Color:    display.PaletteAccent,
			},
			{
				ID:       "eng",
				Label:    "ENG",
				Value:    float64(s.Pips.Eng()),
				Max:      8,
				Segments: 8,
				Color:    display.PaletteAccent,
			},
			{
				ID:       "wep",
				Label:    "WEP",
				Value:    float64(s.Pips.Wep()),
				Max:      8,
				Segments: 8,
				Color:    display.PaletteAccent,
			},
			{
				ID:    "power_draw",
				Label: fmt.Sprintf("Power Draw (%.1f / %.1f MW)", totalDraw, capacity),
				Value: totalDraw,
				Max:   capacity,
				Color: powerBarColor,
			},
			{
				ID:    "fuel_main",
				Label: fmt.Sprintf("Main Fuel (%.1f / %.1f T)", fuelMain, fuelCapMain),
				Value: fuelMain,
				Max:   fuelCapMain,
				Color: fuelBarColor,
			},
			{
				ID:    "fuel_res",
				Label: fmt.Sprintf("Reservoir (%.2f / %.2f T)", fuelRes, fuelCapRes),
				Value: fuelRes,
				Max:   fuelCapRes,
				Color: display.PaletteOK,
			},
		},
	}

	// Tab 2: Status (keyvalue)
	jumpRangeStr := "—"
	if s.MaxJumpRange > 0 {
		jumpRangeStr = fmt.Sprintf("%.2f LY", s.MaxJumpRange)
	}

	overheatingVal := "NOMINAL"
	overheatingColor := display.PaletteOK
	if s.Overheating {
		overheatingVal = "CRITICAL"
		overheatingColor = display.PaletteCrit
	}

	lowFuelVal := "NOMINAL"
	lowFuelColor := display.PaletteOK
	if s.LowFuel {
		lowFuelVal = "WARNING"
		lowFuelColor = display.PaletteWarn
	}

	pct := 0.0
	if capacity > 0 {
		pct = (totalDraw / capacity) * 100.0
	}

	statusTab := display.Tab{
		ID:    "status",
		Title: "STATUS",
		Type:  display.TabTypeKeyValue,
		KeyValues: []display.KeyValue{
			{
				ID:         "jump_range",
				Key:        "Max Jump Range",
				Value:      jumpRangeStr,
				ValueColor: display.PaletteOK,
			},
			{
				ID:         "overheating",
				Key:        "Overheating",
				Value:      overheatingVal,
				ValueColor: overheatingColor,
			},
			{
				ID:         "low_fuel",
				Key:        "Low Fuel Flag",
				Value:      lowFuelVal,
				ValueColor: lowFuelColor,
			},
			{
				ID:         "power_usage",
				Key:        "Power Usage",
				Value:      fmt.Sprintf("%.1f / %.1f MW (%.0f%%)", totalDraw, capacity, pct),
				ValueColor: powerBarColor,
			},
			{
				ID:         "power_plant",
				Key:        "Power Plant",
				Value:      fmt.Sprintf("%.2f MW", capacity),
				ValueColor: display.PaletteNormal,
			},
		},
	}

	// Tab 3: Priorities (list)
	// Modules grouped by priority 1-5 with draw and on/off icon
	priorityModules := make([]ShipModule, len(s.Modules))
	copy(priorityModules, s.Modules)

	sort.SliceStable(priorityModules, func(i, j int) bool {
		pI, pJ := priorityModules[i].Priority, priorityModules[j].Priority
		if pI <= 0 {
			pI = 5
		}
		if pJ <= 0 {
			pJ = 5
		}
		if pI != pJ {
			return pI < pJ
		}
		if priorityModules[i].PowerDraw != priorityModules[j].PowerDraw {
			return priorityModules[i].PowerDraw > priorityModules[j].PowerDraw
		}
		return priorityModules[i].Slot < priorityModules[j].Slot
	})

	prioritiesTab := display.Tab{
		ID:    "priorities",
		Title: "PRIORITIES",
		Type:  display.TabTypeList,
	}

	if len(priorityModules) == 0 {
		prioritiesTab.Items = []display.Item{
			{
				ID:         "placeholder",
				Label:      "No module info",
				Value:      "—",
				ValueColor: display.PaletteDim,
			},
		}
	} else {
		for _, m := range priorityModules {
			p := m.Priority
			if p <= 0 {
				p = 1
			}

			valColor := display.PaletteOK
			icon := ""
			if !m.On {
				valColor = display.PaletteDim
				icon = display.IconWarning
			}

			item := display.Item{
				ID:         fmt.Sprintf("pri_%d_%s", p, strings.ToLower(m.Slot)),
				Label:      m.Label,
				Sublabel:   fmt.Sprintf("Priority %d • %s", p, FormatSlotName(m.Slot)),
				Value:      fmt.Sprintf("%.2f MW", m.PowerDraw),
				ValueColor: valColor,
				Icon:       icon,
				Action:     display.ActionEvent,
			}
			prioritiesTab.Items = append(prioritiesTab.Items, item)
		}
	}

	d.Tabs = []display.Tab{pipsTab, statusTab, prioritiesTab}
	return d
}

// BuildCargoDisplay generates the display model for screens/cargo.json (§12.1).
func (s *ShipState) BuildCargoDisplay() *display.Display {
	s.mu.RLock()
	defer s.mu.RUnlock()

	d := &display.Display{
		SchemaVersion: display.CurrentSchemaVersion,
		Title:         "CARGO",
		Subtitle:      "MANIFEST",
		FooterHint:    "SELECT: COMMODITY",
	}

	var inventory []elite.CargoItem
	if s.Cargo != nil {
		inventory = s.Cargo.Inventory
	}

	used := 0
	stolenCount := 0
	var totalValue int64 = 0
	hasUnknown := false

	type cargoRow struct {
		item      elite.CargoItem
		meanPrice int64
		hasPrice  bool
	}

	rows := make([]cargoRow, 0, len(inventory))
	for _, it := range inventory {
		used += it.Count
		if it.Stolen > 0 {
			stolenCount += it.Stolen
		}

		price, ok := s.lookupPrice(it.Name, it.NameLocalised)
		if ok && price > 0 {
			totalValue += int64(it.Count) * price
		} else {
			hasUnknown = true
		}
		rows = append(rows, cargoRow{
			item:      it,
			meanPrice: price,
			hasPrice:  ok && price > 0,
		})
	}

	maxCap := s.CargoCapacity
	if maxCap <= 0 && s.Cargo != nil && s.Cargo.CargoCapacity > 0 {
		maxCap = s.Cargo.CargoCapacity
	}
	if maxCap <= 0 {
		maxCap = int(math.Max(float64(used), 1.0))
	}

	// Status badge
	badgeColor := display.PaletteOK
	if used >= maxCap && maxCap > 0 {
		badgeColor = display.PaletteWarn
	}
	d.StatusBadge = &display.StatusBadge{
		Text:  fmt.Sprintf("%d / %d T", used, maxCap),
		Color: badgeColor,
	}

	// Tab 1: Inventory (list)
	inventoryTab := display.Tab{
		ID:    "inventory",
		Title: "INVENTORY",
		Type:  display.TabTypeList,
	}

	if len(rows) == 0 {
		inventoryTab.Items = []display.Item{
			{
				ID:         "empty_cargo",
				Label:      "(Empty Cargo Hold)",
				Value:      "0 T",
				ValueColor: display.PaletteDim,
			},
		}
	} else {
		for i, r := range rows {
			label := r.item.NameLocalised
			if label == "" {
				label = FormatModuleName(r.item.Name)
			}

			sub := fmt.Sprintf("%d T", r.item.Count)
			if r.item.Stolen > 0 {
				sub = fmt.Sprintf("%d T (%d Stolen)", r.item.Count, r.item.Stolen)
			}

			valStr := "—"
			valColor := display.PaletteDim
			if r.hasPrice {
				valStr = formatCredits(r.meanPrice) + " CR"
				valColor = display.PaletteNormal
			}

			icon := ""
			if r.item.Stolen > 0 {
				icon = display.IconLock
			}

			clean := elite.CleanItemName(r.item.Name)
			if clean == "" {
				clean = fmt.Sprintf("item_%d", i)
			}

			item := display.Item{
				ID:         fmt.Sprintf("cargo_%s_%d", clean, i),
				Label:      label,
				Sublabel:   sub,
				Value:      valStr,
				ValueColor: valColor,
				Icon:       icon,
				Action:     display.ActionEvent,
			}
			inventoryTab.Items = append(inventoryTab.Items, item)
		}
	}

	// Tab 2: Overview (keyvalue)
	valSuffix := ""
	if hasUnknown && len(rows) > 0 {
		valSuffix = "+"
	}

	overviewTab := display.Tab{
		ID:    "overview",
		Title: "OVERVIEW",
		Type:  display.TabTypeKeyValue,
		KeyValues: []display.KeyValue{
			{
				ID:         "cargo_load",
				Key:        "Used / Max Cargo",
				Value:      fmt.Sprintf("%d / %d T", used, maxCap),
				ValueColor: badgeColor,
			},
			{
				ID:         "total_value",
				Key:        "Total Est. Value",
				Value:      fmt.Sprintf("%s CR%s", formatCredits(totalValue), valSuffix),
				ValueColor: display.PaletteOK,
			},
			{
				ID:         "unique_commodities",
				Key:        "Commodity Types",
				Value:      fmt.Sprintf("%d", len(rows)),
				ValueColor: display.PaletteNormal,
			},
			{
				ID:         "stolen_cargo",
				Key:        "Stolen Cargo",
				Value:      fmt.Sprintf("%d T", stolenCount),
				ValueColor: ternary(stolenCount > 0, display.PaletteWarn, display.PaletteOK),
			},
		},
	}

	// Tab 3: Capacity (bars)
	pctCap := 0.0
	if maxCap > 0 {
		pctCap = (float64(used) / float64(maxCap)) * 100.0
	}
	capBarColor := display.PaletteAccent
	if used >= maxCap {
		capBarColor = display.PaletteWarn
	}

	capacityTab := display.Tab{
		ID:    "capacity",
		Title: "CAPACITY",
		Type:  display.TabTypeBars,
		Bars: []display.Bar{
			{
				ID:    "cargo_capacity_bar",
				Label: fmt.Sprintf("Hold Load (%.0f%%)", pctCap),
				Value: float64(used),
				Max:   float64(maxCap),
				Color: capBarColor,
			},
		},
	}

	d.Tabs = []display.Tab{inventoryTab, overviewTab, capacityTab}
	return d
}

func (s *ShipState) lookupPrice(name, localised string) (int64, bool) {
	if s.PriceCache == nil {
		return 0, false
	}
	if p, ok := s.PriceCache.Get(name); ok {
		return p, true
	}
	if localised != "" {
		if p, ok := s.PriceCache.Get(localised); ok {
			return p, true
		}
	}
	return 0, false
}

func formatShipName(ship string) string {
	clean := strings.ToLower(strings.TrimSpace(ship))
	clean = strings.TrimPrefix(clean, "$")
	clean = strings.TrimSuffix(clean, ";")
	clean = strings.TrimSuffix(clean, "_name")

	switch clean {
	case "anaconda":
		return "Anaconda"
	case "asp":
		return "Asp Explorer"
	case "asp_scout":
		return "Asp Scout"
	case "cobra_mkiii", "cobramkiii":
		return "Cobra Mk III"
	case "cobra_mkiv", "cobramkiv":
		return "Cobra Mk IV"
	case "cutter":
		return "Imperial Cutter"
	case "clipper":
		return "Imperial Clipper"
	case "courier":
		return "Imperial Courier"
	case "diamondback":
		return "Diamondback Scout"
	case "diamondbackxl":
		return "Diamondback Explorer"
	case "dolphin":
		return "Dolphin"
	case "eagle":
		return "Eagle"
	case "empire_eagle":
		return "Imperial Eagle"
	case "ferdelance":
		return "Fer-de-Lance"
	case "hauler":
		return "Hauler"
	case "independant_trader", "keelback":
		return "Keelback"
	case "krait_mkii":
		return "Krait Mk II"
	case "krait_light":
		return "Krait Phantom"
	case "mamba":
		return "Mamba"
	case "orca":
		return "Orca"
	case "python":
		return "Python"
	case "python_nx":
		return "Python Mk II"
	case "sidewinder":
		return "Sidewinder"
	case "type6":
		return "Type-6 Transporter"
	case "type7":
		return "Type-7 Transporter"
	case "type8":
		return "Type-8 Transporter"
	case "type9":
		return "Type-9 Heavy"
	case "type9_military":
		return "Type-10 Defender"
	case "viper":
		return "Viper Mk III"
	case "viper_mkiv":
		return "Viper Mk IV"
	case "vulture":
		return "Vulture"
	}

	if clean == "" {
		return ""
	}
	words := strings.Fields(strings.ReplaceAll(clean, "_", " "))
	for i, w := range words {
		if len(w) > 0 {
			words[i] = strings.ToUpper(w[:1]) + strings.ToLower(w[1:])
		}
	}
	return strings.Join(words, " ")
}

func formatCredits(val int64) string {
	if val < 0 {
		return "-" + formatCredits(-val)
	}
	str := fmt.Sprintf("%d", val)
	n := len(str)
	if n <= 3 {
		return str
	}

	var sb strings.Builder
	rem := n % 3
	if rem > 0 {
		sb.WriteString(str[:rem])
		if n > rem {
			sb.WriteString(",")
		}
	}
	for i := rem; i < n; i += 3 {
		sb.WriteString(str[i : i+3])
		if i+3 < n {
			sb.WriteString(",")
		}
	}
	return sb.String()
}

func defaultStr(val, fallback string) string {
	if val == "" {
		return fallback
	}
	return val
}

func ternary[T any](cond bool, a, b T) T {
	if cond {
		return a
	}
	return b
}
