package main

import (
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"sort"
	"strings"
	"sync"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/events"
)

// OrganicState represents an exobiology species survey progress on a celestial body.
type OrganicState struct {
	Genus     string  `json:"genus"`
	Species   string  `json:"species"`
	Stage     string  `json:"stage"` // "Log", "Sample", "Analyse"
	SampleLat float64 `json:"sampleLat"`
	SampleLon float64 `json:"sampleLon"`
}

// BodyState represents a celestial body and all exploration data recorded for it.
type BodyState struct {
	BodyID                int                      `json:"bodyId"`
	BodyName              string                   `json:"bodyName"`
	PlanetClass           string                   `json:"planetClass,omitempty"`
	StarType              string                   `json:"starType,omitempty"`
	DistanceFromArrivalLS float64                  `json:"distanceFromArrivalLS"`
	Landable              bool                     `json:"landable"`
	TerraformState        string                   `json:"terraformState,omitempty"`
	WasDiscovered         bool                     `json:"wasDiscovered"`
	WasMapped             bool                     `json:"wasMapped"`
	WasFootfalled         bool                     `json:"wasFootfalled"`
	ScanType              string                   `json:"scanType,omitempty"`
	DetailedScan          bool                     `json:"detailedScan"`
	MappedByPilot         bool                     `json:"mappedByPilot"`
	ProbesUsed            int                      `json:"probesUsed"`
	EfficiencyTarget      int                      `json:"efficiencyTarget"`
	BioSignalsCount       int                      `json:"bioSignalsCount"`
	GeoSignalsCount       int                      `json:"geoSignalsCount"`
	Genuses               []string                 `json:"genuses,omitempty"`
	Organics              map[string]*OrganicState `json:"organics,omitempty"`

	// Physical properties for detail inspection
	SurfaceGravity     float64            `json:"surfaceGravity,omitempty"`
	Atmosphere         string             `json:"atmosphere,omitempty"`
	SurfaceTemperature float64            `json:"surfaceTemperature,omitempty"`
	SurfacePressure    float64            `json:"surfacePressure,omitempty"`
	MassEM             float64            `json:"massEM,omitempty"`
	Radius             float64            `json:"radius,omitempty"`
	Rings              []elite.ScanRing   `json:"rings,omitempty"`
}

// SystemState represents exploration and exobiology telemetry for a single star system.
type SystemState struct {
	SystemAddress    int64              `json:"systemAddress"`
	StarSystem       string             `json:"starSystem"`
	StarClass        string             `json:"starClass,omitempty"`
	SystemAllegiance string             `json:"systemAllegiance,omitempty"`
	SystemSecurity   string             `json:"systemSecurity,omitempty"`
	Population       int64              `json:"population,omitempty"`
	BodyCount        int                `json:"bodyCount"`
	AllBodiesFound   bool               `json:"allBodiesFound"`
	Bodies           map[int]*BodyState `json:"bodies"`

	// Real-time location & navigation context
	CurrentBodyID     int     `json:"currentBodyId,omitempty"`
	CurrentBodyName   string  `json:"currentBodyName,omitempty"`
	OnPlanet          bool    `json:"onPlanet"`
	OnFoot            bool    `json:"onFoot"`
	CurrentLat        float64 `json:"currentLat"`
	CurrentLon        float64 `json:"currentLon"`
	PlanetRadius      float64 `json:"planetRadius"`
	HasCurrentCoords  bool    `json:"hasCurrentCoords"`
	InProgressGenus   string  `json:"inProgressGenus,omitempty"`
	InProgressSpecies string  `json:"inProgressSpecies,omitempty"`
	InProgressStage   string  `json:"inProgressStage,omitempty"`
	LastSampleLat     float64 `json:"lastSampleLat"`
	LastSampleLon     float64 `json:"lastSampleLon"`
	HasLastSample     bool    `json:"hasLastSample"`
	SelectedBodyID    int     `json:"selectedBodyId,omitempty"`

	systemsDir string
	mu         sync.Mutex
}

// NewSystemState creates an initialized SystemState.
func NewSystemState(systemsDir string) *SystemState {
	return &SystemState{
		Bodies:     make(map[int]*BodyState),
		systemsDir: systemsDir,
	}
}

// SaveSystemState persists the system state to %LOCALAPPDATA%\EDVR\mfd-companion\systems\<SystemAddress>.json (§12.2).
func SaveSystemState(systemsDir string, s *SystemState) error {
	if s == nil || s.SystemAddress == 0 || systemsDir == "" {
		return nil
	}

	if err := os.MkdirAll(systemsDir, 0o755); err != nil {
		return err
	}

	path := filepath.Join(systemsDir, fmt.Sprintf("%d.json", s.SystemAddress))
	data, err := json.MarshalIndent(s, "", "  ")
	if err != nil {
		return err
	}

	tmpPath := path + ".tmp"
	if err := os.WriteFile(tmpPath, data, 0o644); err != nil {
		return err
	}
	return os.Rename(tmpPath, path)
}

// LoadSystemState loads a persisted system state from disk if available (§12.2).
func LoadSystemState(systemsDir string, systemAddress int64) (*SystemState, error) {
	if systemsDir == "" || systemAddress == 0 {
		return nil, os.ErrNotExist
	}

	path := filepath.Join(systemsDir, fmt.Sprintf("%d.json", systemAddress))
	data, err := os.ReadFile(path)
	if err != nil {
		return nil, err
	}

	var st SystemState
	if err := json.Unmarshal(data, &st); err != nil {
		return nil, err
	}
	st.systemsDir = systemsDir
	if st.Bodies == nil {
		st.Bodies = make(map[int]*BodyState)
	}
	for _, b := range st.Bodies {
		if b.Organics == nil {
			b.Organics = make(map[string]*OrganicState)
		}
	}
	return &st, nil
}

// Save persists the current SystemState to disk.
func (s *SystemState) Save() error {
	return SaveSystemState(s.systemsDir, s)
}

func sanitizeID(s string) string {
	s = strings.ToLower(strings.TrimSpace(s))
	var sb strings.Builder
	for _, r := range s {
		if (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9') || r == '_' || r == '-' {
			sb.WriteRune(r)
		} else if r == ' ' {
			sb.WriteRune('_')
		}
	}
	res := sb.String()
	if res == "" {
		return "item"
	}
	return res
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

func shortBodyName(systemName, bodyName string) string {
	b := strings.TrimSpace(bodyName)
	s := strings.TrimSpace(systemName)
	if s != "" && strings.HasPrefix(b, s) {
		short := strings.TrimSpace(strings.TrimPrefix(b, s))
		if short != "" {
			return short
		}
		return "Main Star"
	}
	if b == "" {
		return "Unknown Body"
	}
	return b
}

func isMappable(planetClass, starType string) bool {
	if starType != "" {
		return false
	}
	p := cleanType(planetClass)
	if p == "" || strings.Contains(p, "belt") || strings.Contains(p, "ring") {
		return false
	}
	return true
}

func cleanSignalGenus(raw string) string {
	raw = strings.TrimPrefix(raw, "$Codex_Ent_")
	raw = strings.TrimSuffix(raw, "_Name;")
	raw = strings.TrimSuffix(raw, "_Genus")
	raw = strings.TrimSuffix(raw, ";")
	raw = strings.ReplaceAll(raw, "_", " ")
	return strings.Title(strings.ToLower(strings.TrimSpace(raw)))
}

// SwitchSystem switches system context on jump or location change (§12.2).
func (s *SystemState) SwitchSystem(newAddr int64, starSystem, allegiance, security string, population int64) {
	if newAddr == s.SystemAddress && s.SystemAddress != 0 {
		// Same system, just update metadata
		if starSystem != "" {
			s.StarSystem = starSystem
		}
		if allegiance != "" {
			s.SystemAllegiance = allegiance
		}
		if security != "" {
			s.SystemSecurity = security
		}
		if population > 0 {
			s.Population = population
		}
		return
	}

	// Persist old system progress
	if s.SystemAddress != 0 {
		_ = s.Save()
	}

	// Try loading existing state from disk
	if loaded, err := LoadSystemState(s.systemsDir, newAddr); err == nil && loaded != nil {
		s.SystemAddress = loaded.SystemAddress
		s.StarSystem = loaded.StarSystem
		s.StarClass = loaded.StarClass
		s.SystemAllegiance = loaded.SystemAllegiance
		s.SystemSecurity = loaded.SystemSecurity
		s.Population = loaded.Population
		s.BodyCount = loaded.BodyCount
		s.AllBodiesFound = loaded.AllBodiesFound
		s.Bodies = loaded.Bodies
	} else {
		// Initialize fresh state
		s.SystemAddress = newAddr
		s.StarSystem = starSystem
		s.StarClass = ""
		s.SystemAllegiance = allegiance
		s.SystemSecurity = security
		s.Population = population
		s.BodyCount = 0
		s.AllBodiesFound = false
		s.Bodies = make(map[int]*BodyState)
	}

	if starSystem != "" {
		s.StarSystem = starSystem
	}
	if allegiance != "" {
		s.SystemAllegiance = allegiance
	}
	if security != "" {
		s.SystemSecurity = security
	}
	if population > 0 {
		s.Population = population
	}

	// Reset view state on system jump/switch per §12.2
	s.CurrentBodyID = 0
	s.CurrentBodyName = ""
	s.SelectedBodyID = 0
	s.OnPlanet = false
	s.OnFoot = false
	s.HasCurrentCoords = false
	s.HasLastSample = false
	s.InProgressGenus = ""
	s.InProgressSpecies = ""
	s.InProgressStage = ""
}

// HandleJournalEntry processes Elite Dangerous Journal events for exploration & exobiology.
func (s *SystemState) HandleJournalEntry(entry elite.JournalEntry) (systemChanged bool, bioChanged bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	switch ev := entry.Data.(type) {
	case *elite.EventFSDJump:
		s.SwitchSystem(ev.SystemAddress, ev.StarSystem, ev.SystemAllegiance, ev.SystemSecurityLocalised, ev.Population)
		return true, true

	case *elite.EventCarrierJump:
		s.SwitchSystem(ev.SystemAddress, ev.StarSystem, ev.SystemAllegiance, ev.SystemSecurity, ev.Population)
		return true, true

	case *elite.EventLocation:
		s.SwitchSystem(ev.SystemAddress, ev.StarSystem, ev.SystemAllegiance, ev.SystemSecurity, ev.Population)
		if ev.BodyID > 0 {
			s.CurrentBodyID = ev.BodyID
			s.CurrentBodyName = ev.Body
		}
		if ev.Latitude != 0 || ev.Longitude != 0 {
			s.CurrentLat = ev.Latitude
			s.CurrentLon = ev.Longitude
			s.HasCurrentCoords = true
		}
		return true, true

	case *elite.EventStartJump:
		if ev.JumpType == "Hyperspace" && ev.StarClass != "" {
			s.StarClass = ev.StarClass
			return true, false
		}

	case *elite.EventFSSDiscoveryScan:
		if ev.SystemAddress == 0 || ev.SystemAddress == s.SystemAddress {
			s.BodyCount = ev.BodyCount
			if ev.SystemName != "" && s.StarSystem == "" {
				s.StarSystem = ev.SystemName
			}
			return true, false
		}

	case *elite.EventFSSAllBodiesFound:
		if ev.SystemAddress == 0 || ev.SystemAddress == s.SystemAddress {
			s.AllBodiesFound = true
			if ev.Count > 0 {
				s.BodyCount = ev.Count
			}
			return true, false
		}

	case *elite.EventScan:
		if ev.SystemAddress != 0 && ev.SystemAddress != s.SystemAddress && s.SystemAddress == 0 {
			s.SystemAddress = ev.SystemAddress
			s.StarSystem = ev.StarSystem
		}
		if ev.SystemAddress != 0 && ev.SystemAddress != s.SystemAddress {
			return false, false
		}

		b, ok := s.Bodies[ev.BodyID]
		if !ok {
			b = &BodyState{
				BodyID:   ev.BodyID,
				Organics: make(map[string]*OrganicState),
			}
			s.Bodies[ev.BodyID] = b
		}

		b.BodyName = ev.BodyName
		b.PlanetClass = ev.PlanetClass
		b.StarType = ev.StarType
		b.DistanceFromArrivalLS = ev.DistanceFromArrivalLS
		b.Landable = ev.Landable
		b.TerraformState = ev.TerraformState
		b.WasDiscovered = ev.WasDiscovered
		b.WasMapped = ev.WasMapped
		b.WasFootfalled = ev.WasFootfalled
		b.ScanType = ev.ScanType
		b.SurfaceGravity = ev.SurfaceGravity
		b.Atmosphere = ev.Atmosphere
		b.SurfaceTemperature = ev.SurfaceTemperature
		b.SurfacePressure = ev.SurfacePressure
		b.MassEM = ev.MassEM
		b.Radius = ev.Radius
		b.Rings = ev.Rings

		if ev.ScanType == "Detailed" || ev.ScanType == "AutoScan" || ev.PlanetClass != "" || ev.StarType != "" {
			b.DetailedScan = true
		}

		if (ev.DistanceFromArrivalLS == 0 || b.BodyID == 0) && ev.StarType != "" && s.StarClass == "" {
			s.StarClass = ev.StarType
		}

		_ = s.Save()
		return true, b.BioSignalsCount > 0

	case *elite.EventSAAScanComplete:
		if ev.SystemAddress != 0 && ev.SystemAddress != s.SystemAddress {
			return false, false
		}
		b, ok := s.Bodies[ev.BodyID]
		if !ok {
			b = &BodyState{
				BodyID:   ev.BodyID,
				BodyName: ev.BodyName,
				Organics: make(map[string]*OrganicState),
			}
			s.Bodies[ev.BodyID] = b
		}
		b.MappedByPilot = true
		b.ProbesUsed = ev.ProbesUsed
		b.EfficiencyTarget = ev.EfficiencyTarget

		_ = s.Save()
		return true, false

	case *elite.EventFSSBodySignals:
		if ev.SystemAddress != 0 && ev.SystemAddress != s.SystemAddress {
			return false, false
		}
		b, ok := s.Bodies[ev.BodyID]
		if !ok {
			b = &BodyState{
				BodyID:   ev.BodyID,
				BodyName: ev.BodyName,
				Organics: make(map[string]*OrganicState),
			}
			s.Bodies[ev.BodyID] = b
		}

		for _, sig := range ev.Signals {
			sigType := strings.ToLower(sig.Type)
			if strings.Contains(sigType, "bio") {
				b.BioSignalsCount = sig.Count
			} else if strings.Contains(sigType, "geo") {
				b.GeoSignalsCount = sig.Count
			}
		}

		_ = s.Save()
		return true, b.BioSignalsCount > 0

	case *elite.EventSAASignalsFound:
		if ev.SystemAddress != 0 && ev.SystemAddress != s.SystemAddress {
			return false, false
		}
		b, ok := s.Bodies[ev.BodyID]
		if !ok {
			b = &BodyState{
				BodyID:   ev.BodyID,
				BodyName: ev.BodyName,
				Organics: make(map[string]*OrganicState),
			}
			s.Bodies[ev.BodyID] = b
		}

		for _, sig := range ev.Signals {
			sigType := strings.ToLower(sig.Type)
			if strings.Contains(sigType, "bio") {
				b.BioSignalsCount = sig.Count
			} else if strings.Contains(sigType, "geo") {
				b.GeoSignalsCount = sig.Count
			}
		}

		for _, gen := range ev.Genuses {
			genusName := gen.GenusLocalised
			if genusName == "" {
				genusName = cleanSignalGenus(gen.Genus)
			}
			found := false
			for _, existing := range b.Genuses {
				if strings.EqualFold(existing, genusName) {
					found = true
					break
				}
			}
			if !found && genusName != "" {
				b.Genuses = append(b.Genuses, genusName)
			}
		}

		_ = s.Save()
		return true, true

	case *elite.EventScanOrganic:
		if ev.SystemAddress != 0 && ev.SystemAddress != s.SystemAddress && s.SystemAddress == 0 {
			s.SystemAddress = ev.SystemAddress
		}
		if ev.SystemAddress != 0 && ev.SystemAddress != s.SystemAddress {
			return false, false
		}

		b, ok := s.Bodies[ev.Body]
		if !ok {
			b = &BodyState{
				BodyID:   ev.Body,
				Organics: make(map[string]*OrganicState),
			}
			s.Bodies[ev.Body] = b
		}
		if b.Organics == nil {
			b.Organics = make(map[string]*OrganicState)
		}

		genusName := ev.GenusLocalised
		if genusName == "" {
			genusName = cleanSignalGenus(ev.Genus)
		}
		speciesName := ev.SpeciesLocalised
		if speciesName == "" {
			speciesName = cleanSignalGenus(ev.Species)
		}

		orgKey := strings.ToLower(speciesName)
		org, ok := b.Organics[orgKey]
		if !ok {
			org = &OrganicState{
				Genus:   genusName,
				Species: speciesName,
			}
			b.Organics[orgKey] = org
		}

		org.Stage = ev.ScanType
		if s.HasCurrentCoords {
			org.SampleLat = s.CurrentLat
			org.SampleLon = s.CurrentLon
		}

		s.InProgressGenus = genusName
		s.InProgressSpecies = speciesName
		s.InProgressStage = ev.ScanType

		if ev.ScanType == "Log" || ev.ScanType == "Sample" {
			if s.HasCurrentCoords {
				s.LastSampleLat = s.CurrentLat
				s.LastSampleLon = s.CurrentLon
				s.HasLastSample = true
			}
		} else if ev.ScanType == "Analyse" {
			s.HasLastSample = false
		}

		_ = s.Save()
		return true, true

	case *elite.EventApproachBody:
		if ev.SystemAddress != 0 && ev.SystemAddress != s.SystemAddress {
			return false, false
		}
		s.CurrentBodyID = ev.BodyID
		s.CurrentBodyName = ev.Body
		return false, true

	case *elite.EventLeaveBody:
		if s.CurrentBodyID == ev.BodyID || s.CurrentBodyName == ev.Body {
			s.CurrentBodyID = 0
			s.CurrentBodyName = ""
			s.OnPlanet = false
		}
		return false, true

	case *elite.EventTouchdown:
		if ev.SystemAddress != 0 && ev.SystemAddress != s.SystemAddress {
			return false, false
		}
		if ev.BodyID > 0 {
			s.CurrentBodyID = ev.BodyID
			s.CurrentBodyName = ev.Body
		}
		s.OnPlanet = ev.OnPlanet
		if ev.PlayerLatitude != 0 || ev.PlayerLongitude != 0 {
			s.CurrentLat = ev.PlayerLatitude
			s.CurrentLon = ev.PlayerLongitude
			s.HasCurrentCoords = true
		}
		return false, true

	case *elite.EventLiftoff:
		s.OnPlanet = false
		if ev.PlayerLatitude != 0 || ev.PlayerLongitude != 0 {
			s.CurrentLat = ev.PlayerLatitude
			s.CurrentLon = ev.PlayerLongitude
			s.HasCurrentCoords = true
		}
		return false, true

	case *elite.EventDisembark:
		s.OnFoot = true
		return false, true

	case *elite.EventSellOrganicData:
		return false, true
	}

	return false, false
}

// HandleStatus processes live coordinate telemetry from Status.json.
func (s *SystemState) HandleStatus(snap elite.StatusSnapshot) (systemChanged bool, bioChanged bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	s.OnFoot = snap.HasFlag2(elite.Flag2OnFoot)
	if snap.BodyName != "" {
		s.CurrentBodyName = snap.BodyName
	}
	if snap.PlanetRadius != nil && *snap.PlanetRadius > 0 {
		s.PlanetRadius = *snap.PlanetRadius
	}

	if snap.Latitude != nil && snap.Longitude != nil {
		s.CurrentLat = *snap.Latitude
		s.CurrentLon = *snap.Longitude
		s.HasCurrentCoords = true

		// If a sample is in progress, any coordinate change triggers live distance update in bio.json
		if s.HasLastSample {
			return false, true
		}
	}

	return false, false
}

// HandleEvent handles MFD interaction events from events.jsonl (§6.2).
func (s *SystemState) HandleEvent(ev events.Event) (systemChanged bool, bioChanged bool) {
	s.mu.Lock()
	defer s.mu.Unlock()

	if ev.Action == "select" {
		if strings.HasPrefix(ev.ItemID, "body_") {
			var id int
			if n, _ := fmt.Sscanf(ev.ItemID, "body_%d", &id); n == 1 {
				s.SelectedBodyID = id
				return true, false
			}
		}
	}
	return false, false
}

// BuildSystemDisplay generates the display model for screens/system.json (Ticket C7).
func (s *SystemState) BuildSystemDisplay() *display.Display {
	s.mu.Lock()
	defer s.mu.Unlock()

	sysName := s.StarSystem
	if sysName == "" {
		sysName = "WAITING FOR SCAN"
	}

	badge := &display.StatusBadge{
		Text:  "WAITING FOR SCAN",
		Color: display.PaletteDim,
	}
	if s.StarSystem != "" {
		if s.AllBodiesFound {
			badge = &display.StatusBadge{Text: "SURVEY COMPLETE", Color: display.PaletteOK}
		} else if len(s.Bodies) > 0 {
			badge = &display.StatusBadge{Text: "SURVEYING", Color: display.PaletteOK}
		} else {
			badge = &display.StatusBadge{Text: "DISCOVERED", Color: display.PaletteAccent}
		}
	}

	// 1. Overview KeyValue tab
	starClass := s.StarClass
	if starClass == "" {
		starClass = "—"
	}
	allegiance := s.SystemAllegiance
	if allegiance == "" {
		allegiance = "None"
	}
	security := s.SystemSecurity
	if security == "" {
		security = "Anarchy"
	}
	popStr := "0"
	if s.Population > 0 {
		popStr = formatCredits(s.Population)
	}

	scannedCount := 0
	var totalSystemValue int64
	for _, b := range s.Bodies {
		if b.DetailedScan {
			scannedCount++
		}
		totalSystemValue += CalculateBodyValue(b.PlanetClass, b.StarType, b.TerraformState, b.MassEM, b.WasDiscovered, b.WasMapped, b.MappedByPilot)
	}

	totalBodies := s.BodyCount
	if totalBodies < len(s.Bodies) {
		totalBodies = len(s.Bodies)
	}
	bodiesCountStr := fmt.Sprintf("Bodies scanned: %d / %d", scannedCount, totalBodies)

	overviewKVs := []display.KeyValue{
		{ID: "system", Key: "System", Value: sysName, ValueColor: display.PaletteNormal},
		{ID: "primary_star", Key: "Primary Star", Value: starClass, ValueColor: display.PaletteAccent},
		{ID: "allegiance", Key: "Allegiance", Value: allegiance, ValueColor: display.PaletteNormal},
		{ID: "security", Key: "Security", Value: security, ValueColor: display.PaletteNormal},
		{ID: "population", Key: "Population", Value: popStr, ValueColor: display.PaletteNormal},
		{ID: "bodies_scanned", Key: "Bodies", Value: bodiesCountStr, ValueColor: display.PaletteOK},
		{ID: "system_value", Key: "Estimated Value", Value: fmt.Sprintf("%s CR", formatCredits(totalSystemValue)), ValueColor: display.PaletteOK},
	}

	// 2. Bodies List tab
	bodyList := make([]*BodyState, 0, len(s.Bodies))
	for _, b := range s.Bodies {
		bodyList = append(bodyList, b)
	}

	// Sort bodies by estimated value descending (§12.2)
	sort.Slice(bodyList, func(i, j int) bool {
		valI := CalculateBodyValue(bodyList[i].PlanetClass, bodyList[i].StarType, bodyList[i].TerraformState, bodyList[i].MassEM, bodyList[i].WasDiscovered, bodyList[i].WasMapped, bodyList[i].MappedByPilot)
		valJ := CalculateBodyValue(bodyList[j].PlanetClass, bodyList[j].StarType, bodyList[j].TerraformState, bodyList[j].MassEM, bodyList[j].WasDiscovered, bodyList[j].WasMapped, bodyList[j].MappedByPilot)
		if valI != valJ {
			return valI > valJ
		}
		return bodyList[i].DistanceFromArrivalLS < bodyList[j].DistanceFromArrivalLS
	})

	bodyItems := make([]display.Item, 0, len(bodyList)+1)
	for _, b := range bodyList {
		val := CalculateBodyValue(b.PlanetClass, b.StarType, b.TerraformState, b.MassEM, b.WasDiscovered, b.WasMapped, b.MappedByPilot)
		shortName := shortBodyName(s.StarSystem, b.BodyName)

		// Sublabel construction: type + ls distance + LAND/TERRA tags
		var typeDesc string
		if b.PlanetClass != "" {
			typeDesc = b.PlanetClass
		} else if b.StarType != "" {
			typeDesc = fmt.Sprintf("Class %s Star", b.StarType)
		} else {
			typeDesc = "Unclassified"
		}

		var tags []string
		tags = append(tags, fmt.Sprintf("%.0f ls", b.DistanceFromArrivalLS))
		if b.Landable {
			tags = append(tags, "LAND")
		}
		if strings.Contains(strings.ToLower(b.TerraformState), "terraform") {
			tags = append(tags, "TERRA")
		}
		sublabel := fmt.Sprintf("%s • %s", typeDesc, strings.Join(tags, " • "))

		// Checks: S, M, B, G
		sState := display.CheckStateTodo
		if b.DetailedScan {
			sState = display.CheckStateDone
		}

		mState := display.CheckStateTodo
		if !isMappable(b.PlanetClass, b.StarType) {
			mState = display.CheckStateNA
		} else if b.MappedByPilot {
			mState = display.CheckStateDone
		}

		bState := display.CheckStateNA
		if b.BioSignalsCount > 0 {
			analysedCount := 0
			for _, org := range b.Organics {
				if org.Stage == "Analyse" {
					analysedCount++
				}
			}
			if analysedCount >= b.BioSignalsCount && b.BioSignalsCount > 0 {
				bState = display.CheckStateDone
			} else {
				bState = display.CheckStateTodo
			}
		}

		gState := display.CheckStateNA
		if b.GeoSignalsCount > 0 {
			gState = display.CheckStateDone
		}

		checks := []display.Check{
			{Label: "S", State: sState},
			{Label: "M", State: mState},
			{Label: "B", State: bState},
			{Label: "G", State: gState},
		}

		// Icon glyphs
		icon := ""
		if b.BioSignalsCount > 0 {
			icon = display.IconBio
		} else if b.GeoSignalsCount > 0 {
			icon = display.IconGeo
		} else if b.StarType != "" {
			icon = display.IconStar
		}

		bodyItems = append(bodyItems, display.Item{
			ID:         fmt.Sprintf("body_%d", b.BodyID),
			Label:      shortName,
			Sublabel:   sublabel,
			Value:      fmt.Sprintf("%s CR", formatCredits(val)),
			ValueColor: display.PaletteOK,
			Icon:       icon,
			Checks:     checks,
			Action:     display.ActionDefault,
		})
	}

	// Trailing unscanned bodies gap (§12.2)
	if s.BodyCount > len(bodyList) {
		gap := s.BodyCount - len(bodyList)
		bodyItems = append(bodyItems, display.Item{
			ID:         "unscanned_gap",
			Label:      fmt.Sprintf("? Unscanned bodies: %d", gap),
			Sublabel:   "Use FSS Discovery Scanner",
			Value:      "—",
			ValueColor: display.PaletteDim,
		})
	}

	if len(bodyItems) == 0 {
		bodyItems = append(bodyItems, display.Item{
			ID:         "no_bodies",
			Label:      "No celestial bodies scanned",
			Sublabel:   "Honk system to begin survey",
			Value:      "—",
			ValueColor: display.PaletteDim,
		})
	}

	tabs := []display.Tab{
		{
			ID:        "overview",
			Title:     "OVERVIEW",
			Type:      display.TabTypeKeyValue,
			KeyValues: overviewKVs,
		},
		{
			ID:    "bodies",
			Title: "BODIES",
			Type:  display.TabTypeList,
			Items: bodyItems,
		},
	}

	// 3. Detail tab 'body' on select (§12.2)
	if s.SelectedBodyID > 0 {
		if sel, ok := s.Bodies[s.SelectedBodyID]; ok {
			pType := sel.PlanetClass
			if pType == "" {
				pType = sel.StarType
			}
			discStr := "Yes"
			if !sel.WasDiscovered {
				discStr = "First Discovery"
			}
			mapStr := "Yes"
			if !sel.WasMapped {
				mapStr = "First Mapped"
			}
			terraStr := sel.TerraformState
			if terraStr == "" {
				terraStr = "No"
			}

			bodyDetailKVs := []display.KeyValue{
				{ID: "det_name", Key: "Name", Value: sel.BodyName, ValueColor: display.PaletteAccent},
				{ID: "det_type", Key: "Classification", Value: pType, ValueColor: display.PaletteNormal},
				{ID: "det_gravity", Key: "Gravity", Value: fmt.Sprintf("%.2f G", sel.SurfaceGravity/9.81), ValueColor: display.PaletteNormal},
				{ID: "det_atmo", Key: "Atmosphere", Value: sel.Atmosphere, ValueColor: display.PaletteNormal},
				{ID: "det_temp", Key: "Surface Temp", Value: fmt.Sprintf("%.0f K", sel.SurfaceTemperature), ValueColor: display.PaletteNormal},
				{ID: "det_pressure", Key: "Pressure", Value: fmt.Sprintf("%.2f atm", sel.SurfacePressure), ValueColor: display.PaletteNormal},
				{ID: "det_terra", Key: "Terraform", Value: terraStr, ValueColor: display.PaletteNormal},
				{ID: "det_disc", Key: "Discovery", Value: discStr, ValueColor: display.PaletteNormal},
				{ID: "det_mapped", Key: "Mapped", Value: mapStr, ValueColor: display.PaletteNormal},
				{ID: "det_bio", Key: "Bio Signals", Value: fmt.Sprintf("%d", sel.BioSignalsCount), ValueColor: display.PaletteNormal},
				{ID: "det_geo", Key: "Geo Signals", Value: fmt.Sprintf("%d", sel.GeoSignalsCount), ValueColor: display.PaletteNormal},
			}
			if len(sel.Rings) > 0 {
				bodyDetailKVs = append(bodyDetailKVs, display.KeyValue{
					ID:         "det_rings",
					Key:        "Rings",
					Value:      fmt.Sprintf("%d ring system(s)", len(sel.Rings)),
					ValueColor: display.PaletteNormal,
				})
			}

			tabs = append(tabs, display.Tab{
				ID:        "body",
				Title:     "BODY",
				Type:      display.TabTypeKeyValue,
				KeyValues: bodyDetailKVs,
			})
		}
	}

	return &display.Display{
		SchemaVersion: display.CurrentSchemaVersion,
		Title:         "SYSTEM",
		Subtitle:      sysName,
		StatusBadge:   badge,
		FooterHint:    "Select body for detailed survey",
		Tabs:          tabs,
	}
}

// BuildBioDisplay generates the display model for screens/bio.json (Ticket C8).
func (s *SystemState) BuildBioDisplay() *display.Display {
	s.mu.Lock()
	defer s.mu.Unlock()

	sysName := s.StarSystem
	if sysName == "" {
		sysName = "WAITING FOR SCAN"
	}

	badge := &display.StatusBadge{
		Text:  "WAITING FOR SCAN",
		Color: display.PaletteDim,
	}
	if s.StarSystem != "" {
		if s.InProgressSpecies != "" {
			badge = &display.StatusBadge{Text: "SAMPLING", Color: display.PaletteAccent}
		} else {
			badge = &display.StatusBadge{Text: "SURVEYING", Color: display.PaletteOK}
		}
	}

	// 1. Bodies tab: bodies in current system with bio signals
	var bioBodies []*BodyState
	for _, b := range s.Bodies {
		if b.BioSignalsCount > 0 || len(b.Genuses) > 0 || len(b.Organics) > 0 {
			bioBodies = append(bioBodies, b)
		}
	}

	sort.Slice(bioBodies, func(i, j int) bool {
		return bioBodies[i].DistanceFromArrivalLS < bioBodies[j].DistanceFromArrivalLS
	})

	bioBodyItems := make([]display.Item, 0, len(bioBodies))
	for _, b := range bioBodies {
		analysed := 0
		var bodyBioValue int64
		mult := int64(1)
		if !b.WasFootfalled {
			mult = 5
		}

		for _, org := range b.Organics {
			if org.Stage == "Analyse" {
				analysed++
			}
			bodyBioValue += GetSpeciesBaseValue(org.Species) * mult
		}

		totalSigs := b.BioSignalsCount
		if len(b.Genuses) > totalSigs {
			totalSigs = len(b.Genuses)
		}

		bState := display.CheckStateTodo
		if analysed >= totalSigs && totalSigs > 0 {
			bState = display.CheckStateDone
		}

		valStr := "—"
		if bodyBioValue > 0 {
			valStr = fmt.Sprintf("%s CR", formatCredits(bodyBioValue))
		}

		bioBodyItems = append(bioBodyItems, display.Item{
			ID:         fmt.Sprintf("bio_body_%d", b.BodyID),
			Label:      shortBodyName(s.StarSystem, b.BodyName),
			Sublabel:   fmt.Sprintf("%d/%d species analysed", analysed, totalSigs),
			Value:      valStr,
			ValueColor: display.PaletteOK,
			Icon:       display.IconBio,
			Checks: []display.Check{
				{Label: "B", State: bState},
			},
			Action: display.ActionDefault,
		})
	}

	if len(bioBodyItems) == 0 {
		bioBodyItems = append(bioBodyItems, display.Item{
			ID:         "no_bio_bodies",
			Label:      "No biological signals detected",
			Sublabel:   "Probe landable planets with DSS",
			Value:      "—",
			ValueColor: display.PaletteDim,
		})
	}

	// 2. Current tab: species/genus logged on current body with L, S, A checks
	var activeBody *BodyState
	if s.CurrentBodyID > 0 {
		activeBody = s.Bodies[s.CurrentBodyID]
	} else if len(bioBodies) == 1 {
		activeBody = bioBodies[0]
	}

	var currentItems []display.Item
	if activeBody != nil {
		mult := int64(1)
		if !activeBody.WasFootfalled {
			mult = 5
		}

		// Genuses known from DSS SAASignalsFound
		genusSet := make(map[string]bool)
		for _, g := range activeBody.Genuses {
			genusSet[strings.ToLower(g)] = true
		}

		// Organics sampled or logged
		for _, org := range activeBody.Organics {
			genusSet[strings.ToLower(org.Genus)] = false // Covered by specific species row

			lState := display.CheckStateDone
			sState := display.CheckStateTodo
			aState := display.CheckStateTodo
			badgeText := "LOGGED"

			if org.Stage == "Sample" {
				sState = display.CheckStateDone
				badgeText = "2/3 SAMPLED"
			} else if org.Stage == "Analyse" {
				sState = display.CheckStateDone
				aState = display.CheckStateDone
				badgeText = "ANALYSED"
			}

			colonyReq := GetGenusColonyRange(org.Genus)
			specVal := GetSpeciesBaseValue(org.Species) * mult

			currentItems = append(currentItems, display.Item{
				ID:         fmt.Sprintf("org_%s", sanitizeID(org.Species)),
				Label:      org.Species,
				Sublabel:   fmt.Sprintf("%s • Colony: %.0fm", org.Genus, colonyReq),
				Value:      fmt.Sprintf("%s CR", formatCredits(specVal)),
				ValueColor: display.PaletteOK,
				Badge:      badgeText,
				Checks: []display.Check{
					{Label: "L", State: lState},
					{Label: "S", State: sState},
					{Label: "A", State: aState},
				},
				Action: display.ActionDefault,
			})
		}

		// Any remaining genuses without logged species
		for g, unlogged := range genusSet {
			if unlogged {
				colonyReq := GetGenusColonyRange(g)
				currentItems = append(currentItems, display.Item{
					ID:         fmt.Sprintf("genus_%s", sanitizeID(g)),
					Label:      strings.Title(g),
					Sublabel:   fmt.Sprintf("Colony: %.0fm • Awaiting sample log", colonyReq),
					Value:      "—",
					ValueColor: display.PaletteDim,
					Checks: []display.Check{
						{Label: "L", State: display.CheckStateTodo},
						{Label: "S", State: display.CheckStateTodo},
						{Label: "A", State: display.CheckStateTodo},
					},
					Action: display.ActionDefault,
				})
			}
		}
	}

	if len(currentItems) == 0 {
		currentItems = append(currentItems, display.Item{
			ID:         "no_current_bio",
			Label:      "No active biological survey",
			Sublabel:   "Approach or land on body with bio signals",
			Value:      "—",
			ValueColor: display.PaletteDim,
		})
	}

	// 3. Sample KeyValue tab: live distance from last sample via Haversine (§12.2)
	var sampleKVs []display.KeyValue
	if s.HasLastSample && s.HasCurrentCoords && s.InProgressSpecies != "" {
		radius := s.PlanetRadius
		if radius <= 0 && activeBody != nil && activeBody.Radius > 0 {
			radius = activeBody.Radius
		}
		dist := HaversineDistance(s.LastSampleLat, s.LastSampleLon, s.CurrentLat, s.CurrentLon, radius)
		reqDist := GetGenusColonyRange(s.InProgressGenus)

		distColor := display.PaletteCrit
		statusText := "TOO CLOSE (Clonal Colony)"
		if dist >= reqDist {
			distColor = display.PaletteOK
			statusText = "DIVERSE COLONY READY"
		}

		stageNum := 1
		if s.InProgressStage == "Sample" {
			stageNum = 2
		} else if s.InProgressStage == "Analyse" {
			stageNum = 3
		}

		sampleKVs = []display.KeyValue{
			{ID: "sample_dist", Key: "Sample Distance", Value: fmt.Sprintf("%.0f m / %.0f m required", dist, reqDist), ValueColor: distColor},
			{ID: "sample_status", Key: "Status", Value: statusText, ValueColor: distColor},
			{ID: "sample_species", Key: "Species", Value: s.InProgressSpecies, ValueColor: display.PaletteAccent},
			{ID: "sample_genus", Key: "Genus", Value: s.InProgressGenus, ValueColor: display.PaletteNormal},
			{ID: "sample_colony", Key: "Colony Range", Value: fmt.Sprintf("%.0f m", reqDist), ValueColor: display.PaletteNormal},
			{ID: "sample_stage", Key: "Sample Stage", Value: fmt.Sprintf("%s (%d/3)", s.InProgressStage, stageNum), ValueColor: display.PaletteNormal},
			{ID: "sample_pos", Key: "Position", Value: fmt.Sprintf("%.4f°, %.4f°", s.CurrentLat, s.CurrentLon), ValueColor: display.PaletteDim},
		}
	} else {
		sampleKVs = []display.KeyValue{
			{ID: "sample_dist", Key: "Sample Distance", Value: "—", ValueColor: display.PaletteDim},
			{ID: "sample_status", Key: "Status", Value: "Awaiting sample in progress", ValueColor: display.PaletteDim},
		}
		if s.InProgressSpecies != "" {
			sampleKVs = append(sampleKVs, display.KeyValue{
				ID:         "sample_species",
				Key:        "Species",
				Value:      s.InProgressSpecies,
				ValueColor: display.PaletteAccent,
			})
		}
	}

	bodiesTab := display.Tab{
		ID:    "bodies",
		Title: "BODIES",
		Type:  display.TabTypeList,
		Items: bioBodyItems,
	}
	currentTab := display.Tab{
		ID:    "current",
		Title: "CURRENT",
		Type:  display.TabTypeList,
		Items: currentItems,
	}
	sampleTab := display.Tab{
		ID:        "sample",
		Title:     "SAMPLE",
		Type:      display.TabTypeKeyValue,
		KeyValues: sampleKVs,
	}

	// Ordering per §12.2: current tab is default when on/near a body
	var tabs []display.Tab
	if s.CurrentBodyID > 0 || (activeBody != nil && len(activeBody.Organics) > 0) {
		tabs = []display.Tab{currentTab, sampleTab, bodiesTab}
	} else {
		tabs = []display.Tab{bodiesTab, currentTab, sampleTab}
	}

	return &display.Display{
		SchemaVersion: display.CurrentSchemaVersion,
		Title:         "BIO",
		Subtitle:      sysName,
		StatusBadge:   badge,
		FooterHint:    "Live colony distance updates on foot",
		Tabs:          tabs,
	}
}
