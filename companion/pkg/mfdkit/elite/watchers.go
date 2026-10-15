package elite

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"math"
	"os"
	"path/filepath"
	"strings"
	"time"
)

// Pips represents half-pips allocated to [SYS, ENG, WEP]. Max 8 half-pips each (4 pips).
type Pips [3]int

// Sys returns the system half-pips (0-8).
func (p Pips) Sys() int { return p[0] }

// Eng returns the engine half-pips (0-8).
func (p Pips) Eng() int { return p[1] }

// Wep returns the weapons half-pips (0-8).
func (p Pips) Wep() int { return p[2] }

// SysFloat returns system pips as 0.0-4.0.
func (p Pips) SysFloat() float64 { return float64(p[0]) / 2.0 }

// EngFloat returns engine pips as 0.0-4.0.
func (p Pips) EngFloat() float64 { return float64(p[1]) / 2.0 }

// WepFloat returns weapon pips as 0.0-4.0.
func (p Pips) WepFloat() float64 { return float64(p[2]) / 2.0 }

// UnmarshalJSON unmarshals Pips from either int or float JSON arrays.
func (p *Pips) UnmarshalJSON(data []byte) error {
	data = bytes.TrimSpace(data)
	if len(data) == 0 || string(data) == "null" {
		*p = Pips{0, 0, 0}
		return nil
	}
	var raw []float64
	if err := json.Unmarshal(data, &raw); err != nil {
		return err
	}
	for i := 0; i < len(raw) && i < 3; i++ {
		(*p)[i] = int(math.Round(raw[i]))
	}
	return nil
}

// StatusFuel represents fuel values from Status.json.
type StatusFuel struct {
	FuelMain      float64 `json:"FuelMain"`
	FuelReservoir float64 `json:"FuelReservoir"`
}

// UnmarshalJSON unmarshals StatusFuel from either an object or a single float (legacy SRV).
func (f *StatusFuel) UnmarshalJSON(data []byte) error {
	data = bytes.TrimSpace(data)
	if len(data) == 0 || string(data) == "null" {
		return nil
	}
	if data[0] == '{' {
		type alias StatusFuel
		var a alias
		if err := json.Unmarshal(data, &a); err != nil {
			return err
		}
		*f = StatusFuel(a)
		return nil
	}
	var val float64
	if err := json.Unmarshal(data, &val); err != nil {
		return err
	}
	f.FuelMain = val
	return nil
}

// StatusSnapshot represents the parsed state from Status.json per §11 & §12.
type StatusSnapshot struct {
	Timestamp    time.Time       `json:"timestamp"`
	Event        string          `json:"event"`
	Flags        uint64          `json:"Flags"`
	Flags2       uint64          `json:"Flags2"`
	Pips         Pips            `json:"Pips"`
	FireGroup    int             `json:"FireGroup"`
	GuiFocus     int             `json:"GuiFocus"`
	Fuel         StatusFuel      `json:"Fuel"`
	Cargo        float64         `json:"Cargo"`
	LegalState   string          `json:"LegalState"`
	Latitude     *float64        `json:"Latitude,omitempty"`
	Longitude    *float64        `json:"Longitude,omitempty"`
	Altitude     *float64        `json:"Altitude,omitempty"`
	Heading      *float64        `json:"Heading,omitempty"`
	PlanetRadius *float64        `json:"PlanetRadius,omitempty"`
	BodyName     string          `json:"BodyName,omitempty"`
	Raw          json.RawMessage `json:"-"`
}

// HasFlag returns true if the specified Flags bit is set.
func (s StatusSnapshot) HasFlag(flag uint64) bool {
	return (s.Flags & flag) != 0
}

// HasFlag2 returns true if the specified Flags2 bit is set.
func (s StatusSnapshot) HasFlag2(flag uint64) bool {
	return (s.Flags2 & flag) != 0
}

func (s StatusSnapshot) IsDocked() bool       { return s.HasFlag(FlagDocked) }
func (s StatusSnapshot) IsLanded() bool       { return s.HasFlag(FlagLanded) }
func (s StatusSnapshot) InSupercruise() bool  { return s.HasFlag(FlagSupercruise) }
func (s StatusSnapshot) Overheating() bool    { return s.HasFlag(FlagOverheating) }
func (s StatusSnapshot) LowFuel() bool        { return s.HasFlag(FlagLowFuel) }
func (s StatusSnapshot) OnFoot() bool         { return s.HasFlag2(Flag2OnFoot) }
func (s StatusSnapshot) InSRV() bool          { return s.HasFlag(FlagInSRV) }
func (s StatusSnapshot) InFighter() bool      { return s.HasFlag(FlagInFighter) }
func (s StatusSnapshot) InMainShip() bool     { return s.HasFlag(FlagInMainShip) }
func (s StatusSnapshot) GlideMode() bool      { return s.HasFlag2(Flag2GlideMode) }

// CargoItem represents an inventory entry in Cargo.json.
type CargoItem struct {
	Name          string `json:"Name"`
	NameLocalised string `json:"Name_Localised,omitempty"`
	Count         int    `json:"Count"`
	Stolen        int    `json:"Stolen"`
	MissionID     int64  `json:"MissionID,omitempty"`
}

// CargoSnapshot represents the parsed state from Cargo.json.
type CargoSnapshot struct {
	Timestamp     time.Time       `json:"timestamp"`
	Event         string          `json:"event"`
	Vessel        string          `json:"Vessel"`
	Count         int             `json:"Count"`
	CargoCapacity int             `json:"CargoCapacity,omitempty"`
	Inventory     []CargoItem     `json:"Inventory"`
	Raw           json.RawMessage `json:"-"`
}

// TotalInventoryCount returns the sum of counts across all inventory items.
func (c CargoSnapshot) TotalInventoryCount() int {
	total := 0
	for _, item := range c.Inventory {
		total += item.Count
	}
	return total
}

// FindItem searches for an item by internal name or localized name.
func (c CargoSnapshot) FindItem(name string) *CargoItem {
	needle := strings.ToLower(name)
	for i := range c.Inventory {
		if strings.ToLower(c.Inventory[i].Name) == needle ||
			strings.ToLower(c.Inventory[i].NameLocalised) == needle {
			return &c.Inventory[i]
		}
	}
	return nil
}

// MarketItem represents an item entry in Market.json.
type MarketItem struct {
	ID                int64  `json:"id"`
	Name              string `json:"Name"`
	NameLocalised     string `json:"Name_Localised,omitempty"`
	Category          string `json:"Category"`
	CategoryLocalised string `json:"Category_Localised,omitempty"`
	BuyPrice          int64  `json:"BuyPrice"`
	SellPrice         int64  `json:"SellPrice"`
	MeanPrice         int64  `json:"MeanPrice"`
	StockBracket      int    `json:"StockBracket"`
	DemandBracket     int    `json:"DemandBracket"`
	Stock             int64  `json:"Stock"`
	Demand            int64  `json:"Demand"`
	Consumer          bool   `json:"Consumer"`
	Producer          bool   `json:"Producer"`
	Rare              bool   `json:"Rare"`
}

// MarketSnapshot represents the parsed state from Market.json.
type MarketSnapshot struct {
	Timestamp   time.Time        `json:"timestamp"`
	Event       string           `json:"event"`
	MarketID    int64            `json:"MarketID"`
	StationName string           `json:"StationName"`
	StationType string           `json:"StationType,omitempty"`
	StarSystem  string           `json:"StarSystem"`
	Items       []MarketItem     `json:"Items"`
	MeanPrices  map[string]int64 `json:"-"` // Normalized lookup map
	Raw         json.RawMessage  `json:"-"`
}

// CleanItemName strips leading '$' and trailing '_name;' from Elite item symbols.
// Example: "$powergenerators_name;" -> "powergenerators".
func CleanItemName(name string) string {
	s := strings.TrimPrefix(name, "$")
	s = strings.TrimSuffix(s, ";")
	s = strings.TrimSuffix(s, "_name")
	return strings.ToLower(strings.TrimSpace(s))
}

// BuildMeanPrices builds a normalized price lookup cache from the Items slice.
func (m *MarketSnapshot) BuildMeanPrices() {
	m.MeanPrices = make(map[string]int64, len(m.Items)*3)
	for _, item := range m.Items {
		price := item.MeanPrice
		if item.Name != "" {
			m.MeanPrices[item.Name] = price
			m.MeanPrices[strings.ToLower(item.Name)] = price
			clean := CleanItemName(item.Name)
			if clean != "" {
				m.MeanPrices[clean] = price
			}
		}
		if item.NameLocalised != "" {
			m.MeanPrices[item.NameLocalised] = price
			m.MeanPrices[strings.ToLower(item.NameLocalised)] = price
		}
	}
}

// MeanPrice returns the galactic average MeanPrice for the given item name.
func (m MarketSnapshot) MeanPrice(itemName string) (int64, bool) {
	if m.MeanPrices == nil {
		m.BuildMeanPrices()
	}
	if price, ok := m.MeanPrices[itemName]; ok {
		return price, true
	}
	needle := strings.ToLower(itemName)
	if price, ok := m.MeanPrices[needle]; ok {
		return price, true
	}
	clean := CleanItemName(itemName)
	if price, ok := m.MeanPrices[clean]; ok {
		return price, true
	}
	return 0, false
}

// RouteEntry represents a waypoint in NavRoute.json.
type RouteEntry struct {
	StarSystem    string     `json:"StarSystem"`
	SystemAddress int64      `json:"SystemAddress"`
	StarPos       [3]float64 `json:"StarPos"`
	StarClass     string     `json:"StarClass"`
}

// NavRouteSnapshot represents the parsed state from NavRoute.json.
type NavRouteSnapshot struct {
	Timestamp time.Time       `json:"timestamp"`
	Event     string          `json:"event"`
	Route     []RouteEntry    `json:"Route"`
	Raw       json.RawMessage `json:"-"`
}

// TotalDistance calculates the total route distance by summing Euclidean deltas along Route.
func (n NavRouteSnapshot) TotalDistance() float64 {
	if len(n.Route) < 2 {
		return 0
	}
	var total float64
	for i := 1; i < len(n.Route); i++ {
		p1 := n.Route[i-1].StarPos
		p2 := n.Route[i].StarPos
		dx := p2[0] - p1[0]
		dy := p2[1] - p1[1]
		dz := p2[2] - p1[2]
		total += math.Sqrt(dx*dx + dy*dy + dz*dz)
	}
	return total
}

// NextHop returns the hop immediately following the specified current system name.
func (n NavRouteSnapshot) NextHop(currentSystem string) *RouteEntry {
	for i := 0; i < len(n.Route)-1; i++ {
		if strings.EqualFold(n.Route[i].StarSystem, currentSystem) {
			return &n.Route[i+1]
		}
	}
	return nil
}

// RemainingJumps returns the number of jumps remaining from the given current system name.
func (n NavRouteSnapshot) RemainingJumps(currentSystem string) int {
	for i := 0; i < len(n.Route); i++ {
		if strings.EqualFold(n.Route[i].StarSystem, currentSystem) {
			return len(n.Route) - 1 - i
		}
	}
	return len(n.Route)
}

// ModuleInfoEntry represents a module row in ModuleInfo.json.
type ModuleInfoEntry struct {
	Slot     string  `json:"Slot"`
	Item     string  `json:"Item"`
	Power    float64 `json:"Power"`
	Priority int     `json:"Priority"`
}

// ModuleInfoSnapshot represents the parsed state from ModuleInfo.json.
type ModuleInfoSnapshot struct {
	Timestamp time.Time         `json:"timestamp"`
	Event     string            `json:"event"`
	Modules   []ModuleInfoEntry `json:"Modules"`
	Raw       json.RawMessage   `json:"-"`
}

// FindSlot finds a module entry by slot name (case-insensitive).
func (m ModuleInfoSnapshot) FindSlot(slot string) *ModuleInfoEntry {
	for i := range m.Modules {
		if strings.EqualFold(m.Modules[i].Slot, slot) {
			return &m.Modules[i]
		}
	}
	return nil
}

// WatcherOption configures snapshot watcher behavior.
type WatcherOption func(*watcherConfig)

type watcherConfig struct {
	pollInterval time.Duration
	initialRead  bool
	maxRetries   int
	retryDelay   time.Duration
}

// WithWatchPollInterval configures the polling cadence.
func WithWatchPollInterval(d time.Duration) WatcherOption {
	return func(c *watcherConfig) {
		if d > 0 {
			c.pollInterval = d
		}
	}
}

// WithWatchInitialRead configures whether to emit the initial file state immediately if present.
func WithWatchInitialRead(read bool) WatcherOption {
	return func(c *watcherConfig) {
		c.initialRead = read
	}
}

// WithWatchMaxRetries configures retry count for partial write races.
func WithWatchMaxRetries(n int) WatcherOption {
	return func(c *watcherConfig) {
		if n >= 0 {
			c.maxRetries = n
		}
	}
}

// WithWatchRetryDelay configures backoff delay between read retries.
func WithWatchRetryDelay(d time.Duration) WatcherOption {
	return func(c *watcherConfig) {
		if d > 0 {
			c.retryDelay = d
		}
	}
}

func defaultWatcherConfig() watcherConfig {
	return watcherConfig{
		pollInterval: 50 * time.Millisecond,
		initialRead:  true,
		maxRetries:   5,
		retryDelay:   10 * time.Millisecond,
	}
}

func resolveSnapshotPath(dirOrFile string, filename string) string {
	if strings.HasSuffix(strings.ToLower(dirOrFile), ".json") {
		return dirOrFile
	}
	return filepath.Join(dirOrFile, filename)
}

// ReadStatus reads and parses Status.json from the specified path.
func ReadStatus(path string) (StatusSnapshot, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return StatusSnapshot{}, err
	}
	var snap StatusSnapshot
	if err := json.Unmarshal(data, &snap); err != nil {
		return StatusSnapshot{}, err
	}
	snap.Raw = data
	return snap, nil
}

// ReadCargo reads and parses Cargo.json from the specified path.
func ReadCargo(path string) (CargoSnapshot, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return CargoSnapshot{}, err
	}
	var snap CargoSnapshot
	if err := json.Unmarshal(data, &snap); err != nil {
		return CargoSnapshot{}, err
	}
	snap.Raw = data
	return snap, nil
}

// ReadMarket reads and parses Market.json from the specified path.
func ReadMarket(path string) (MarketSnapshot, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return MarketSnapshot{}, err
	}
	var snap MarketSnapshot
	if err := json.Unmarshal(data, &snap); err != nil {
		return MarketSnapshot{}, err
	}
	snap.Raw = data
	snap.BuildMeanPrices()
	return snap, nil
}

// ReadNavRoute reads and parses NavRoute.json from the specified path.
func ReadNavRoute(path string) (NavRouteSnapshot, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return NavRouteSnapshot{}, err
	}
	var snap NavRouteSnapshot
	if err := json.Unmarshal(data, &snap); err != nil {
		return NavRouteSnapshot{}, err
	}
	snap.Raw = data
	return snap, nil
}

// ReadModuleInfo reads and parses ModuleInfo.json from the specified path.
func ReadModuleInfo(path string) (ModuleInfoSnapshot, error) {
	data, err := os.ReadFile(path)
	if err != nil {
		return ModuleInfoSnapshot{}, err
	}
	var snap ModuleInfoSnapshot
	if err := json.Unmarshal(data, &snap); err != nil {
		return ModuleInfoSnapshot{}, err
	}
	snap.Raw = data
	return snap, nil
}

// watchFileLoop provides robust polling, partial write race retries, and deduplication.
func watchFileLoop[T any](
	ctx context.Context,
	path string,
	cfg watcherConfig,
	parseFunc func(path string) (T, error),
	outCh chan<- T,
	errCh chan<- error,
) {
	defer close(outCh)
	defer close(errCh)

	var lastBytes []byte
	var lastModTime time.Time
	var lastSize int64

	readWithRetry := func() (T, []byte, error) {
		var zero T
		var lastErr error
		for attempt := 0; attempt <= cfg.maxRetries; attempt++ {
			if attempt > 0 {
				select {
				case <-ctx.Done():
					return zero, nil, ctx.Err()
				case <-time.After(cfg.retryDelay):
				}
			}

			raw, err := os.ReadFile(path)
			if err != nil {
				lastErr = err
				continue
			}
			if len(raw) == 0 {
				lastErr = fmt.Errorf("empty file read")
				continue
			}

			val, err := parseFunc(path)
			if err != nil {
				lastErr = err
				continue
			}
			return val, raw, nil
		}
		return zero, nil, lastErr
	}

	ticker := time.NewTicker(cfg.pollInterval)
	defer ticker.Stop()

	// Initial check
	if cfg.initialRead {
		if fi, err := os.Stat(path); err == nil && !fi.IsDir() {
			lastModTime = fi.ModTime()
			lastSize = fi.Size()
			val, raw, err := readWithRetry()
			if err == nil {
				lastBytes = raw
				select {
				case outCh <- val:
				case <-ctx.Done():
					return
				}
			} else {
				select {
				case errCh <- err:
				default:
				}
			}
		}
	}

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			fi, err := os.Stat(path)
			if err != nil {
				// File does not exist yet or temporarily inaccessible
				continue
			}
			if fi.IsDir() {
				continue
			}

			// If neither mod time nor size changed, avoid re-reading
			if fi.ModTime().Equal(lastModTime) && fi.Size() == lastSize && len(lastBytes) > 0 {
				continue
			}

			val, raw, err := readWithRetry()
			if err != nil {
				select {
				case errCh <- err:
				default:
				}
				continue
			}

			lastModTime = fi.ModTime()
			lastSize = fi.Size()

			if bytes.Equal(raw, lastBytes) {
				continue
			}
			lastBytes = raw

			select {
			case outCh <- val:
			case <-ctx.Done():
				return
			}
		}
	}
}

// WatchStatus watches Status.json in dirOrFile and emits updates when changed.
func WatchStatus(ctx context.Context, dirOrFile string, opts ...WatcherOption) (<-chan StatusSnapshot, <-chan error) {
	cfg := defaultWatcherConfig()
	for _, opt := range opts {
		opt(&cfg)
	}
	path := resolveSnapshotPath(dirOrFile, "Status.json")
	outCh := make(chan StatusSnapshot, 32)
	errCh := make(chan error, 16)
	go watchFileLoop(ctx, path, cfg, ReadStatus, outCh, errCh)
	return outCh, errCh
}

// WatchCargo watches Cargo.json in dirOrFile and emits updates when changed.
func WatchCargo(ctx context.Context, dirOrFile string, opts ...WatcherOption) (<-chan CargoSnapshot, <-chan error) {
	cfg := defaultWatcherConfig()
	for _, opt := range opts {
		opt(&cfg)
	}
	path := resolveSnapshotPath(dirOrFile, "Cargo.json")
	outCh := make(chan CargoSnapshot, 32)
	errCh := make(chan error, 16)
	go watchFileLoop(ctx, path, cfg, ReadCargo, outCh, errCh)
	return outCh, errCh
}

// WatchMarket watches Market.json in dirOrFile and emits updates when changed.
func WatchMarket(ctx context.Context, dirOrFile string, opts ...WatcherOption) (<-chan MarketSnapshot, <-chan error) {
	cfg := defaultWatcherConfig()
	for _, opt := range opts {
		opt(&cfg)
	}
	path := resolveSnapshotPath(dirOrFile, "Market.json")
	outCh := make(chan MarketSnapshot, 32)
	errCh := make(chan error, 16)
	go watchFileLoop(ctx, path, cfg, ReadMarket, outCh, errCh)
	return outCh, errCh
}

// WatchNavRoute watches NavRoute.json in dirOrFile and emits updates when changed.
func WatchNavRoute(ctx context.Context, dirOrFile string, opts ...WatcherOption) (<-chan NavRouteSnapshot, <-chan error) {
	cfg := defaultWatcherConfig()
	for _, opt := range opts {
		opt(&cfg)
	}
	path := resolveSnapshotPath(dirOrFile, "NavRoute.json")
	outCh := make(chan NavRouteSnapshot, 32)
	errCh := make(chan error, 16)
	go watchFileLoop(ctx, path, cfg, ReadNavRoute, outCh, errCh)
	return outCh, errCh
}

// WatchModuleInfo watches ModuleInfo.json in dirOrFile and emits updates when changed.
func WatchModuleInfo(ctx context.Context, dirOrFile string, opts ...WatcherOption) (<-chan ModuleInfoSnapshot, <-chan error) {
	cfg := defaultWatcherConfig()
	for _, opt := range opts {
		opt(&cfg)
	}
	path := resolveSnapshotPath(dirOrFile, "ModuleInfo.json")
	outCh := make(chan ModuleInfoSnapshot, 32)
	errCh := make(chan error, 16)
	go watchFileLoop(ctx, path, cfg, ReadModuleInfo, outCh, errCh)
	return outCh, errCh
}
