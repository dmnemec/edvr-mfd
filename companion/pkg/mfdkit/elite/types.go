package elite

import (
	"bytes"
	"encoding/json"
	"fmt"
	"time"
)

// JournalEntry represents a single parsed record from an Elite Dangerous Journal file.
type JournalEntry struct {
	Timestamp time.Time       `json:"timestamp"`
	Event     string          `json:"event"`
	Raw       json.RawMessage `json:"-"`
	Data      any             `json:"-"` // Strongly-typed event struct if recognized, or map[string]any
}

// FuelCapacityInfo represents fuel capacity which can appear as either a number or an object in Loadout.
type FuelCapacityInfo struct {
	Main    float64 `json:"Main"`
	Reserve float64 `json:"Reserve"`
}

// UnmarshalJSON unmarshals FuelCapacityInfo from either a float or {"Main": ..., "Reserve": ...}.
func (f *FuelCapacityInfo) UnmarshalJSON(data []byte) error {
	data = bytes.TrimSpace(data)
	if len(data) == 0 {
		return nil
	}
	if data[0] == '{' {
		type alias FuelCapacityInfo
		var a alias
		if err := json.Unmarshal(data, &a); err != nil {
			return err
		}
		*f = FuelCapacityInfo(a)
		return nil
	}
	var val float64
	if err := json.Unmarshal(data, &val); err != nil {
		return err
	}
	f.Main = val
	return nil
}

// EngineeringModifier represents a single engineering modification on a module.
type EngineeringModifier struct {
	Label         string  `json:"Label"`
	Value         float64 `json:"Value"`
	OriginalValue float64 `json:"OriginalValue,omitempty"`
	LessIsGood    int     `json:"LessIsGood,omitempty"`
}

// ModuleEngineering represents engineering information for a ship module.
type ModuleEngineering struct {
	Engineer      string                `json:"Engineer,omitempty"`
	EngineerID    int64                 `json:"EngineerID,omitempty"`
	BlueprintID   int64                 `json:"BlueprintID,omitempty"`
	BlueprintName string                `json:"BlueprintName,omitempty"`
	Level         int                   `json:"Level,omitempty"`
	Quality       float64               `json:"Quality,omitempty"`
	Modifiers     []EngineeringModifier `json:"Modifiers,omitempty"`
}

// LoadoutModule represents a single module installed on a ship in the Loadout event.
type LoadoutModule struct {
	Slot         string             `json:"Slot"`
	Item         string             `json:"Item"`
	On           bool               `json:"On"`
	Priority     int                `json:"Priority"`
	Health       float64            `json:"Health"`
	Value        int64              `json:"Value,omitempty"`
	AmmoInClip   int                `json:"AmmoInClip,omitempty"`
	AmmoInHopper int                `json:"AmmoInHopper,omitempty"`
	Engineering  *ModuleEngineering `json:"Engineering,omitempty"`
}

// EventLoadout is dispatched on ship loadout changes or at startup.
type EventLoadout struct {
	Timestamp     time.Time        `json:"timestamp"`
	Event         string           `json:"event"`
	Ship          string           `json:"Ship"`
	ShipID        int              `json:"ShipID"`
	ShipName      string           `json:"ShipName"`
	ShipIdent     string           `json:"ShipIdent"`
	HullValue     int64            `json:"HullValue,omitempty"`
	ModulesValue  int64            `json:"ModulesValue,omitempty"`
	HullHealth    float64          `json:"HullHealth"`
	UnladenMass   float64          `json:"UnladenMass,omitempty"`
	CargoCapacity int              `json:"CargoCapacity"`
	MaxJumpRange  float64          `json:"MaxJumpRange"`
	FuelCapacity  FuelCapacityInfo `json:"FuelCapacity"`
	Rebuy         int64            `json:"Rebuy,omitempty"`
	Modules       []LoadoutModule  `json:"Modules"`
}

// EventHullDamage is emitted when taking hull damage.
type EventHullDamage struct {
	Timestamp time.Time `json:"timestamp"`
	Event     string    `json:"event"`
	Health    float64   `json:"Health"`
	Player    bool      `json:"Player"`
	Fighter   bool      `json:"Fighter"`
}

// EventAfmuRepairs is emitted when the Auto Field-Maintenance Unit repairs a module.
type EventAfmuRepairs struct {
	Timestamp     time.Time `json:"timestamp"`
	Event         string    `json:"event"`
	Module        string    `json:"Module"`
	FullyRepaired bool      `json:"FullyRepaired"`
	Health        float64   `json:"Health"`
}

// EventRepair is emitted when repairing an item at a station.
type EventRepair struct {
	Timestamp time.Time `json:"timestamp"`
	Event     string    `json:"event"`
	Item      string    `json:"Item"`
	Cost      int64     `json:"Cost"`
}

// EventRepairAll is emitted when repairing all items at a station.
type EventRepairAll struct {
	Timestamp time.Time `json:"timestamp"`
	Event     string    `json:"event"`
	Cost      int64     `json:"Cost"`
}

// EventRebootRepair is emitted when rebooting the ship to repair modules.
type EventRebootRepair struct {
	Timestamp time.Time `json:"timestamp"`
	Event     string    `json:"event"`
	Modules   []string  `json:"Modules"`
}

// EventModuleSwap is emitted when swapping two installed modules.
type EventModuleSwap struct {
	Timestamp time.Time `json:"timestamp"`
	Event     string    `json:"event"`
	MarketID  int64     `json:"MarketID,omitempty"`
	FromSlot  string    `json:"FromSlot"`
	ToSlot    string    `json:"ToSlot"`
	FromItem  string    `json:"FromItem"`
	ToItem    string    `json:"ToItem"`
	Ship      string    `json:"Ship"`
	ShipID    int       `json:"ShipID"`
}

// EventModuleBuy is emitted when purchasing a new module.
type EventModuleBuy struct {
	Timestamp  time.Time `json:"timestamp"`
	Event      string    `json:"event"`
	MarketID   int64     `json:"MarketID,omitempty"`
	Slot       string    `json:"Slot"`
	BuyItem    string    `json:"BuyItem"`
	BuyPrice   int64     `json:"BuyPrice"`
	Ship       string    `json:"Ship"`
	ShipID     int       `json:"ShipID"`
	SellItem   string    `json:"SellItem,omitempty"`
	SellPrice  int64     `json:"SellPrice,omitempty"`
	StoredItem string    `json:"StoredItem,omitempty"`
}

// EventModuleSell is emitted when selling an installed module.
type EventModuleSell struct {
	Timestamp time.Time `json:"timestamp"`
	Event     string    `json:"event"`
	MarketID  int64     `json:"MarketID,omitempty"`
	Slot      string    `json:"Slot"`
	SellItem  string    `json:"SellItem"`
	SellPrice int64     `json:"SellPrice"`
	Ship      string    `json:"Ship"`
	ShipID    int       `json:"ShipID"`
}

// EventModuleStore is emitted when storing an installed module.
type EventModuleStore struct {
	Timestamp              time.Time `json:"timestamp"`
	Event                  string    `json:"event"`
	MarketID               int64     `json:"MarketID,omitempty"`
	Slot                   string    `json:"Slot"`
	StoredItem             string    `json:"StoredItem"`
	Ship                   string    `json:"Ship"`
	ShipID                 int       `json:"ShipID"`
	EngineerModifications string    `json:"EngineerModifications,omitempty"`
	ReplacementItem        string    `json:"ReplacementItem,omitempty"`
	Cost                   int64     `json:"Cost,omitempty"`
}

// EventModuleRetrieve is emitted when retrieving a stored module.
type EventModuleRetrieve struct {
	Timestamp   time.Time `json:"timestamp"`
	Event       string    `json:"event"`
	MarketID    int64     `json:"MarketID,omitempty"`
	Slot        string    `json:"Slot"`
	RetrievedItem string  `json:"RetrievedItem"`
	Ship        string    `json:"Ship"`
	ShipID      int       `json:"ShipID"`
	SwapOutItem string    `json:"SwapOutItem,omitempty"`
	Cost        int64     `json:"Cost,omitempty"`
}

// EventJetConeBoost is emitted when supercharging the Frame Shift Drive in a jet cone.
type EventJetConeBoost struct {
	BoostValue float64 `json:"BoostValue"`
}

// EventFSDJump is emitted when completing a Frame Shift Drive jump.
type EventFSDJump struct {
	Timestamp              time.Time  `json:"timestamp"`
	Event                  string     `json:"event"`
	StarSystem             string     `json:"StarSystem"`
	SystemAddress          int64      `json:"SystemAddress"`
	StarPos                [3]float64 `json:"StarPos"`
	SystemAllegiance       string     `json:"SystemAllegiance,omitempty"`
	SystemEconomy          string     `json:"SystemEconomy,omitempty"`
	SystemEconomyLocalised string     `json:"SystemEconomy_Localised,omitempty"`
	SystemSecondEconomy    string     `json:"SystemSecondEconomy,omitempty"`
	SystemGovernment       string     `json:"SystemGovernment,omitempty"`
	SystemGovernmentLocalised string  `json:"SystemGovernment_Localised,omitempty"`
	SystemSecurity         string     `json:"SystemSecurity,omitempty"`
	SystemSecurityLocalised string    `json:"SystemSecurity_Localised,omitempty"`
	Population             int64      `json:"Population,omitempty"`
	Body                   string     `json:"Body,omitempty"`
	BodyID                 int        `json:"BodyID,omitempty"`
	BodyType               string     `json:"BodyType,omitempty"`
	JumpDist               float64    `json:"JumpDist,omitempty"`
	FuelUsed               float64    `json:"FuelUsed,omitempty"`
	FuelLevel              float64    `json:"FuelLevel,omitempty"`
}

// EventCarrierJump is emitted when jumping aboard a Fleet Carrier.
type EventCarrierJump struct {
	Timestamp        time.Time  `json:"timestamp"`
	Event            string     `json:"event"`
	StarSystem       string     `json:"StarSystem"`
	SystemAddress    int64      `json:"SystemAddress"`
	StarPos          [3]float64 `json:"StarPos"`
	Body             string     `json:"Body,omitempty"`
	BodyID           int        `json:"BodyID,omitempty"`
	BodyType         string     `json:"BodyType,omitempty"`
	SystemAllegiance string     `json:"SystemAllegiance,omitempty"`
	SystemEconomy    string     `json:"SystemEconomy,omitempty"`
	SystemGovernment string     `json:"SystemGovernment,omitempty"`
	SystemSecurity   string     `json:"SystemSecurity,omitempty"`
	Population       int64      `json:"Population,omitempty"`
}

// EventLocation is emitted at startup or when changing locations/stations.
type EventLocation struct {
	Timestamp        time.Time  `json:"timestamp"`
	Event            string     `json:"event"`
	StarSystem       string     `json:"StarSystem"`
	SystemAddress    int64      `json:"SystemAddress"`
	StarPos          [3]float64 `json:"StarPos"`
	Body             string     `json:"Body,omitempty"`
	BodyID           int        `json:"BodyID,omitempty"`
	BodyType         string     `json:"BodyType,omitempty"`
	DistFromStarLS   float64    `json:"DistFromStarLS,omitempty"`
	Docked           bool       `json:"Docked"`
	StationName      string     `json:"StationName,omitempty"`
	StationType      string     `json:"StationType,omitempty"`
	MarketID         int64      `json:"MarketID,omitempty"`
	SystemAllegiance string     `json:"SystemAllegiance,omitempty"`
	SystemEconomy    string     `json:"SystemEconomy,omitempty"`
	SystemGovernment string     `json:"SystemGovernment,omitempty"`
	SystemSecurity   string     `json:"SystemSecurity,omitempty"`
	Population       int64      `json:"Population,omitempty"`
	Latitude         float64    `json:"Latitude,omitempty"`
	Longitude        float64    `json:"Longitude,omitempty"`
}

// EventStartJump is emitted when beginning hyperspace or supercruise jump charge.
type EventStartJump struct {
	Timestamp     time.Time `json:"timestamp"`
	Event         string    `json:"event"`
	JumpType      string    `json:"JumpType"` // "Hyperspace" or "Supercruise"
	StarSystem    string    `json:"StarSystem,omitempty"`
	SystemAddress int64     `json:"SystemAddress,omitempty"`
	StarClass     string    `json:"StarClass,omitempty"`
}

// EventFSSDiscoveryScan is emitted when performing an FSS Discovery "honk".
type EventFSSDiscoveryScan struct {
	Timestamp     time.Time `json:"timestamp"`
	Event         string    `json:"event"`
	Progress      float64   `json:"Progress"`
	BodyCount     int       `json:"BodyCount"`
	NonBodyCount  int       `json:"NonBodyCount,omitempty"`
	SystemName    string    `json:"SystemName,omitempty"`
	SystemAddress int64     `json:"SystemAddress,omitempty"`
}

// EventFSSAllBodiesFound is emitted when all bodies in the system have been discovered.
type EventFSSAllBodiesFound struct {
	Timestamp     time.Time `json:"timestamp"`
	Event         string    `json:"event"`
	SystemName    string    `json:"SystemName"`
	SystemAddress int64     `json:"SystemAddress"`
	Count         int       `json:"Count"`
}

// ScanAtmosphereComp represents one component of an atmosphere in a Scan event.
type ScanAtmosphereComp struct {
	Name    string  `json:"Name"`
	Percent float64 `json:"Percent"`
}

// ScanMaterial represents surface material composition.
type ScanMaterial struct {
	Name    string  `json:"Name"`
	Percent float64 `json:"Percent"`
}

// ScanRing represents a planetary ring or asteroid belt.
type ScanRing struct {
	Name      string  `json:"Name"`
	RingClass string  `json:"RingClass"`
	MassMT    float64 `json:"MassMT"`
	InnerRad  float64 `json:"InnerRad"`
	OuterRad  float64 `json:"OuterRad"`
}

// ScanParent represents parent hierarchy for bodies.
type ScanParent struct {
	Null      *int `json:"Null,omitempty"`
	Star      *int `json:"Star,omitempty"`
	Planet    *int `json:"Planet,omitempty"`
}

// EventScan is emitted when scanning a celestial body.
type EventScan struct {
	Timestamp              time.Time            `json:"timestamp"`
	Event                  string               `json:"event"`
	ScanType               string               `json:"ScanType"`
	BodyName               string               `json:"BodyName"`
	BodyID                 int                  `json:"BodyID"`
	StarSystem             string               `json:"StarSystem,omitempty"`
	SystemAddress          int64                `json:"SystemAddress,omitempty"`
	DistanceFromArrivalLS  float64              `json:"DistanceFromArrivalLS"`
	StarType               string               `json:"StarType,omitempty"`
	Subclass               int                  `json:"Subclass,omitempty"`
	StellarMass            float64              `json:"StellarMass,omitempty"`
	Radius                 float64              `json:"Radius,omitempty"`
	AbsoluteMagnitude      float64              `json:"AbsoluteMagnitude,omitempty"`
	AgeMY                  int                  `json:"Age_MY,omitempty"`
	SurfaceTemperature     float64              `json:"SurfaceTemperature,omitempty"`
	PlanetClass            string               `json:"PlanetClass,omitempty"`
	Atmosphere             string               `json:"Atmosphere,omitempty"`
	AtmosphereType         string               `json:"AtmosphereType,omitempty"`
	AtmosphereComposition  []ScanAtmosphereComp `json:"AtmosphereComposition,omitempty"`
	Volcanism              string               `json:"Volcanism,omitempty"`
	MassEM                 float64              `json:"MassEM,omitempty"`
	SurfaceGravity         float64              `json:"SurfaceGravity,omitempty"`
	SurfacePressure        float64              `json:"SurfacePressure,omitempty"`
	Landable               bool                 `json:"Landable,omitempty"`
	Materials              []ScanMaterial       `json:"Materials,omitempty"`
	Composition            map[string]float64   `json:"Composition,omitempty"`
	TerraformState         string               `json:"TerraformState,omitempty"`
	WasDiscovered          bool                 `json:"WasDiscovered"`
	WasMapped              bool                 `json:"WasMapped"`
	WasFootfalled          bool                 `json:"WasFootfalled,omitempty"`
	Rings                  []ScanRing           `json:"Rings,omitempty"`
	ReserveLevel           string               `json:"ReserveLevel,omitempty"`
}

// EventSAAScanComplete is emitted when mapping a body with the Detailed Surface Scanner.
type EventSAAScanComplete struct {
	Timestamp        time.Time `json:"timestamp"`
	Event            string    `json:"event"`
	BodyName         string    `json:"BodyName"`
	BodyID           int       `json:"BodyID"`
	SystemAddress    int64     `json:"SystemAddress,omitempty"`
	ProbesUsed       int       `json:"ProbesUsed"`
	EfficiencyTarget int       `json:"EfficiencyTarget"`
}

// BodySignal represents a signal (biological, geological, human) detected on a body.
type BodySignal struct {
	Type          string `json:"Type"`
	TypeLocalised string `json:"Type_Localised,omitempty"`
	Count         int    `json:"Count"`
}

// EventFSSBodySignals is emitted when signals are detected on a body via FSS.
type EventFSSBodySignals struct {
	Timestamp     time.Time    `json:"timestamp"`
	Event         string       `json:"event"`
	BodyName      string       `json:"BodyName"`
	BodyID        int          `json:"BodyID"`
	SystemAddress int64        `json:"SystemAddress"`
	Signals       []BodySignal `json:"Signals"`
}

// GenusSignal represents a biological genus discovered on a body.
type GenusSignal struct {
	Genus          string `json:"Genus"`
	GenusLocalised string `json:"Genus_Localised,omitempty"`
}

// EventSAASignalsFound is emitted when the DSS finishes mapping a body with surface signals.
type EventSAASignalsFound struct {
	Timestamp     time.Time     `json:"timestamp"`
	Event         string        `json:"event"`
	BodyName      string        `json:"BodyName"`
	BodyID        int           `json:"BodyID"`
	SystemAddress int64         `json:"SystemAddress"`
	Signals       []BodySignal  `json:"Signals,omitempty"`
	Genuses       []GenusSignal `json:"Genuses,omitempty"`
}

// EventScanOrganic is emitted during exobiology sampling (Log, Sample, Analyse).
type EventScanOrganic struct {
	Timestamp        time.Time `json:"timestamp"`
	Event            string    `json:"event"`
	ScanType         string    `json:"ScanType"` // "Log", "Sample", "Analyse"
	Genus            string    `json:"Genus"`
	GenusLocalised   string    `json:"Genus_Localised,omitempty"`
	Species          string    `json:"Species"`
	SpeciesLocalised string    `json:"Species_Localised,omitempty"`
	Variant          string    `json:"Variant,omitempty"`
	VariantLocalised string    `json:"Variant_Localised,omitempty"`
	Body             int       `json:"Body"`
	SystemAddress    int64     `json:"SystemAddress"`
}

// EventApproachBody is emitted when approaching a celestial body.
type EventApproachBody struct {
	Timestamp     time.Time `json:"timestamp"`
	Event         string    `json:"event"`
	StarSystem    string    `json:"StarSystem"`
	SystemAddress int64     `json:"SystemAddress"`
	Body          string    `json:"Body"`
	BodyID        int       `json:"BodyID"`
}

// EventLeaveBody is emitted when leaving a celestial body.
type EventLeaveBody struct {
	Timestamp     time.Time `json:"timestamp"`
	Event         string    `json:"event"`
	StarSystem    string    `json:"StarSystem"`
	SystemAddress int64     `json:"SystemAddress"`
	Body          string    `json:"Body"`
	BodyID        int       `json:"BodyID"`
}

// EventTouchdown is emitted when ship or SRV touches down on planetary surface or station pad.
type EventTouchdown struct {
	Timestamp          time.Time `json:"timestamp"`
	Event              string    `json:"event"`
	PlayerLatitude     float64   `json:"PlayerLatitude,omitempty"`
	PlayerLongitude    float64   `json:"PlayerLongitude,omitempty"`
	NearestDestination string    `json:"NearestDestination,omitempty"`
	StarSystem         string    `json:"StarSystem,omitempty"`
	SystemAddress      int64     `json:"SystemAddress,omitempty"`
	Body               string    `json:"Body,omitempty"`
	BodyID             int       `json:"BodyID,omitempty"`
	OnPlanet           bool      `json:"OnPlanet,omitempty"`
	OnStation          bool      `json:"OnStation,omitempty"`
}

// EventLiftoff is emitted when taking off from planet surface or landing pad.
type EventLiftoff struct {
	Timestamp       time.Time `json:"timestamp"`
	Event           string    `json:"event"`
	PlayerLatitude  float64   `json:"PlayerLatitude,omitempty"`
	PlayerLongitude float64   `json:"PlayerLongitude,omitempty"`
	StarSystem      string    `json:"StarSystem,omitempty"`
	SystemAddress   int64     `json:"SystemAddress,omitempty"`
	Body            string    `json:"Body,omitempty"`
	BodyID          int       `json:"BodyID,omitempty"`
	OnPlanet        bool      `json:"OnPlanet,omitempty"`
	OnStation       bool      `json:"OnStation,omitempty"`
}

// EventDisembark is emitted when disembarking on foot.
type EventDisembark struct {
	Timestamp     time.Time `json:"timestamp"`
	Event         string    `json:"event"`
	SRV           bool      `json:"SRV,omitempty"`
	Taxi          bool      `json:"Taxi,omitempty"`
	Multicrew     bool      `json:"Multicrew,omitempty"`
	StarSystem    string    `json:"StarSystem,omitempty"`
	SystemAddress int64     `json:"SystemAddress,omitempty"`
	Body          string    `json:"Body,omitempty"`
	BodyID        int       `json:"BodyID,omitempty"`
	OnPlanet      bool      `json:"OnPlanet,omitempty"`
	OnStation     bool      `json:"OnStation,omitempty"`
}

// BioDataRecord represents one sold organic species record.
type BioDataRecord struct {
	Genus   string `json:"Genus"`
	Species string `json:"Species"`
	Value   int64  `json:"Value"`
	Bonus   int64  `json:"Bonus,omitempty"`
}

// EventSellOrganicData is emitted when selling exobiology data at Vista Genomics.
type EventSellOrganicData struct {
	Timestamp     time.Time       `json:"timestamp"`
	Event         string          `json:"event"`
	MarketID      int64           `json:"MarketID,omitempty"`
	BioData       []BioDataRecord `json:"BioData"`
	TotalEarnings int64           `json:"TotalEarnings"`
}

// EventLoadGame is emitted when logging into the game.
type EventLoadGame struct {
	Timestamp    time.Time `json:"timestamp"`
	Event        string    `json:"event"`
	Commander    string    `json:"Commander"`
	FID          string    `json:"FID,omitempty"`
	Horizons     bool      `json:"Horizons,omitempty"`
	Odyssey      bool      `json:"Odyssey,omitempty"`
	Ship         string    `json:"Ship"`
	ShipID       int       `json:"ShipID"`
	ShipName     string    `json:"ShipName,omitempty"`
	ShipIdent    string    `json:"ShipIdent,omitempty"`
	FuelLevel    float64   `json:"FuelLevel,omitempty"`
	FuelCapacity float64   `json:"FuelCapacity,omitempty"`
	GameMode     string    `json:"GameMode,omitempty"`
	Credits      int64     `json:"Credits,omitempty"`
	Loan         int64     `json:"Loan,omitempty"`
}

// EventFileheader is the first record in a Journal file.
type EventFileheader struct {
	Timestamp   time.Time `json:"timestamp"`
	Event       string    `json:"event"`
	Part        int       `json:"part"`
	Language    string    `json:"language"`
	GameVersion string    `json:"gameversion"`
	Build       string    `json:"build"`
}

// EventNavRouteClear is emitted when a plotted route is cleared.
type EventNavRouteClear struct {
	Timestamp time.Time `json:"timestamp"`
	Event     string    `json:"event"`
}

// ParseJournalEntry parses a raw JSON line into a JournalEntry with strongly-typed Data if known.
func ParseJournalEntry(line []byte) (JournalEntry, error) {
	line = bytes.TrimSpace(line)
	if len(line) == 0 {
		return JournalEntry{}, fmt.Errorf("empty line")
	}

	var header struct {
		Timestamp time.Time `json:"timestamp"`
		Event     string    `json:"event"`
	}
	if err := json.Unmarshal(line, &header); err != nil {
		return JournalEntry{}, fmt.Errorf("failed to parse journal line header: %w", err)
	}

	entry := JournalEntry{
		Timestamp: header.Timestamp,
		Event:     header.Event,
		Raw:       bytes.Clone(line),
	}

	switch header.Event {
	// Ship / Loadout / Modules
	case "Loadout":
		var ev EventLoadout
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "HullDamage":
		var ev EventHullDamage
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "AfmuRepairs":
		var ev EventAfmuRepairs
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "Repair":
		var ev EventRepair
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "RepairAll":
		var ev EventRepairAll
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "RebootRepair":
		var ev EventRebootRepair
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "ModuleSwap":
		var ev EventModuleSwap
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "ModuleBuy":
		var ev EventModuleBuy
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "ModuleSell":
		var ev EventModuleSell
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "ModuleStore":
		var ev EventModuleStore
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "ModuleRetrieve":
		var ev EventModuleRetrieve
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}

	// Navigation / Location
	case "FSDJump":
		var ev EventFSDJump
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "JetConeBoost":
		var ev EventJetConeBoost
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "CarrierJump":
		var ev EventCarrierJump
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "Location":
		var ev EventLocation
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "StartJump":
		var ev EventStartJump
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}

	// Exploration
	case "FSSDiscoveryScan":
		var ev EventFSSDiscoveryScan
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "FSSAllBodiesFound":
		var ev EventFSSAllBodiesFound
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "Scan":
		var ev EventScan
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "SAAScanComplete":
		var ev EventSAAScanComplete
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "FSSBodySignals":
		var ev EventFSSBodySignals
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "SAASignalsFound":
		var ev EventSAASignalsFound
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}

	// Exobiology
	case "ScanOrganic":
		var ev EventScanOrganic
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "ApproachBody":
		var ev EventApproachBody
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "LeaveBody":
		var ev EventLeaveBody
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "Touchdown":
		var ev EventTouchdown
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "Liftoff":
		var ev EventLiftoff
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "Disembark":
		var ev EventDisembark
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "SellOrganicData":
		var ev EventSellOrganicData
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}

	// General / Lifecycle
	case "LoadGame":
		var ev EventLoadGame
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "Fileheader":
		var ev EventFileheader
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}
	case "NavRouteClear":
		var ev EventNavRouteClear
		if err := json.Unmarshal(line, &ev); err == nil {
			entry.Data = &ev
		}

	// Fallback to generic map[string]any for unknown events
	default:
		var generic map[string]any
		if err := json.Unmarshal(line, &generic); err == nil {
			entry.Data = generic
		}
	}

	return entry, nil
}
