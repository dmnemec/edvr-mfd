package main

import (
	"fmt"
	"math"
	"strings"
	"sync"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/events"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/writer"
)

// StarClassIcon returns the schema glyph icon for a star class per §12.3:
// - "neutron" for 'N'
// - "whitedwarf" for 'D*'
// - "fuel" for KGBFOAM scoopable stars
func StarClassIcon(starClass string) string {
	trimmed := strings.TrimSpace(starClass)
	if len(trimmed) == 0 {
		return ""
	}
	upper := strings.ToUpper(trimmed)
	if upper == "N" || strings.HasPrefix(upper, "N ") || strings.HasPrefix(upper, "NEUTRON") || (len(upper) >= 2 && upper[0] == 'N' && upper[1] >= '0' && upper[1] <= '9') {
		return display.IconNeutron
	}
	if strings.HasPrefix(upper, "D") {
		if len(upper) == 1 || upper[1] == ' ' || upper[1] == 'A' || upper[1] == 'B' || upper[1] == 'O' || upper[1] == 'Z' || upper[1] == 'V' || upper[1] == 'Q' || upper[1] == 'C' || upper[1] == 'X' {
			return display.IconWhiteDwarf
		}
	}

	switch upper[0] {
	case 'K', 'G', 'B', 'F', 'O', 'A', 'M':
		if len(upper) == 1 || upper[1] == ' ' || (upper[1] >= '0' && upper[1] <= '9') {
			return display.IconFuel
		}
		return ""
	default:
		return ""
	}
}

// CalculateRemainingDistance sums Euclidean distances along route from fromIdx to the end.
func CalculateRemainingDistance(route []elite.RouteEntry, fromIdx int) float64 {
	if fromIdx < 0 {
		fromIdx = 0
	}
	if len(route) < 2 || fromIdx >= len(route)-1 {
		return 0
	}
	var total float64
	for i := fromIdx; i < len(route)-1; i++ {
		p1 := route[i].StarPos
		p2 := route[i+1].StarPos
		dx := p2[0] - p1[0]
		dy := p2[1] - p1[1]
		dz := p2[2] - p1[2]
		total += math.Sqrt(dx*dx + dy*dy + dz*dz)
	}
	return total
}

// RouteEngine manages route state and drives display screen generation.
type RouteEngine struct {
	mu sync.Mutex

	currentSystem string
	navRoute      *elite.NavRouteSnapshot

	spanshWaypoints []SpanshWaypoint
	currSpanshIdx   int
	autoCopy        bool
	spanshCSVPath   string
	iniPath         string

	clipboard   Clipboard
	writer      *writer.CoalescedWriter
	destPath    string
	onWriteHook func(d *display.Display)
}

// EngineOption configures RouteEngine.
type EngineOption func(*RouteEngine)

// WithClipboard overrides the clipboard implementation.
func WithClipboard(cb Clipboard) EngineOption {
	return func(e *RouteEngine) {
		e.clipboard = cb
	}
}

// WithWriter sets the coalesced writer.
func WithWriter(w *writer.CoalescedWriter, destPath string) EngineOption {
	return func(e *RouteEngine) {
		e.writer = w
		e.destPath = destPath
	}
}

// WithWriteHook sets a test hook called whenever a new display is generated and queued.
func WithWriteHook(hook func(d *display.Display)) EngineOption {
	return func(e *RouteEngine) {
		e.onWriteHook = hook
	}
}

// WithIniPath configures the route.ini path.
func WithIniPath(iniPath string) EngineOption {
	return func(e *RouteEngine) {
		e.iniPath = iniPath
	}
}

// NewRouteEngine creates a new RouteEngine instance.
func NewRouteEngine(spanshCSVPath string, opts ...EngineOption) *RouteEngine {
	e := &RouteEngine{
		currSpanshIdx: -1,
		spanshCSVPath: spanshCSVPath,
		clipboard:     defaultClipboard,
	}
	for _, opt := range opts {
		opt(e)
	}

	if e.iniPath != "" {
		cfg := LoadRouteINI(e.iniPath)
		e.autoCopy = cfg.AutoCopy
		if cfg.RouteCSV != "" {
			e.spanshCSVPath = cfg.RouteCSV
		}
	}

	e.ReloadSpansh()
	return e
}

// ReloadSpansh reloads the Spansh CSV file if available.
func (e *RouteEngine) ReloadSpansh() {
	e.mu.Lock()
	defer e.mu.Unlock()

	if e.spanshCSVPath == "" {
		return
	}
	wps, err := LoadSpanshRoute(e.spanshCSVPath)
	if err == nil {
		e.spanshWaypoints = wps
		e.matchSpanshProgressLocked()
	} else {
		e.spanshWaypoints = nil
		e.currSpanshIdx = -1
	}
}

func (e *RouteEngine) matchSpanshProgressLocked() {
	e.currSpanshIdx = -1
	if e.currentSystem == "" || len(e.spanshWaypoints) == 0 {
		return
	}
	for i, wp := range e.spanshWaypoints {
		if strings.EqualFold(wp.SystemName, e.currentSystem) {
			e.currSpanshIdx = i
			break
		}
	}
}

// HandleLocation handles initial game location.
func (e *RouteEngine) HandleLocation(system string) {
	e.mu.Lock()
	defer e.mu.Unlock()

	e.currentSystem = system
	e.matchSpanshProgressLocked()
	e.syncScreenLocked()
}

// HandleFSDJump handles a jump to a new system.
func (e *RouteEngine) HandleFSDJump(system string) {
	e.mu.Lock()
	defer e.mu.Unlock()

	e.currentSystem = system
	prevIdx := e.currSpanshIdx
	e.matchSpanshProgressLocked()

	// If arrived at a new Spansh waypoint and autoCopy is enabled, copy the next waypoint
	if e.autoCopy && e.currSpanshIdx >= 0 && e.currSpanshIdx != prevIdx {
		nextIdx := e.currSpanshIdx + 1
		if nextIdx < len(e.spanshWaypoints) {
			nextSystem := e.spanshWaypoints[nextIdx].SystemName
			if e.clipboard != nil {
				_ = e.clipboard.SetText(nextSystem)
			}
		}
	}

	e.syncScreenLocked()
}

// HandleNavRoute updates route snapshot from NavRoute.json.
func (e *RouteEngine) HandleNavRoute(snap elite.NavRouteSnapshot) {
	e.mu.Lock()
	defer e.mu.Unlock()

	e.navRoute = &snap
	e.syncScreenLocked()
}

// HandleNavRouteClear handles route cancellation.
func (e *RouteEngine) HandleNavRouteClear() {
	e.mu.Lock()
	defer e.mu.Unlock()

	e.navRoute = nil
	e.syncScreenLocked()
}

// HandleEvent handles MFD interaction events from events.jsonl (§6.2).
func (e *RouteEngine) HandleEvent(ev events.Event) {
	e.mu.Lock()
	defer e.mu.Unlock()

	isAutoCopyToggle := (ev.ItemID == "auto_copy") || (ev.Key != nil && *ev.Key == "auto_copy")
	if (ev.Type == events.TypeSelect || ev.Type == events.TypeAdjust) && isAutoCopyToggle {
		e.autoCopy = !e.autoCopy
		if e.iniPath != "" {
			_ = SaveRouteINI(e.iniPath, RouteConfig{
				AutoCopy: e.autoCopy,
				RouteCSV: e.spanshCSVPath,
			})
		}
		e.syncScreenLocked()
		return
	}

	if ev.Type == events.TypeSelect && ev.Action == display.ActionClipboard {
		if ev.Label != "" && e.clipboard != nil {
			_ = e.clipboard.SetText(ev.Label)
		}
	}
}

// ToggleAutoCopy explicitly toggles auto-copy state.
func (e *RouteEngine) ToggleAutoCopy() bool {
	e.mu.Lock()
	defer e.mu.Unlock()

	e.autoCopy = !e.autoCopy
	if e.iniPath != "" {
		_ = SaveRouteINI(e.iniPath, RouteConfig{
			AutoCopy: e.autoCopy,
			RouteCSV: e.spanshCSVPath,
		})
	}
	e.syncScreenLocked()
	return e.autoCopy
}

// BuildDisplay builds the complete Schema v1 Display model.
func (e *RouteEngine) BuildDisplay() *display.Display {
	e.mu.Lock()
	defer e.mu.Unlock()

	return e.buildDisplayLocked()
}

func (e *RouteEngine) buildDisplayLocked() *display.Display {
	d := &display.Display{
		SchemaVersion: display.CurrentSchemaVersion,
		Title:         "ROUTE",
		Subtitle:      "STANDBY",
		StatusBadge: &display.StatusBadge{
			Text:  "WAITING FOR ROUTE",
			Color: display.ColorDim,
		},
		FooterHint: "SELECT: COPY TO CLIPBOARD",
	}

	// 1. NAVROUTE Tab
	navTab := e.buildNavRouteTabLocked(d)

	// 2. SPANSH Tab
	spanshTab := e.buildSpanshTabLocked()

	d.Tabs = []display.Tab{navTab, spanshTab}
	return d
}

func (e *RouteEngine) buildNavRouteTabLocked(d *display.Display) display.Tab {
	if e.navRoute == nil || len(e.navRoute.Route) == 0 {
		return display.Tab{
			ID:    "navroute",
			Title: "NAVROUTE",
			Type:  display.TabTypeText,
			Text:  "No route plotted.\nPlot route in Galaxy Map\nto view waypoints.",
		}
	}

	route := e.navRoute.Route
	currIdx := -1
	if e.currentSystem != "" {
		for i, r := range route {
			if strings.EqualFold(r.StarSystem, e.currentSystem) {
				currIdx = i
				break
			}
		}
	}

	var jumpsRemaining int
	var nextJumpSystem string
	var totalDist float64

	if currIdx >= 0 {
		jumpsRemaining = len(route) - 1 - currIdx
		totalDist = CalculateRemainingDistance(route, currIdx)
		if currIdx+1 < len(route) {
			nextJumpSystem = route[currIdx+1].StarSystem
		} else {
			nextJumpSystem = "DESTINATION"
		}
	} else {
		jumpsRemaining = len(route) - 1
		totalDist = e.navRoute.TotalDistance()
		if len(route) > 0 {
			nextJumpSystem = route[0].StarSystem
		}
	}

	d.Subtitle = fmt.Sprintf("%d JUMPS | %.1f LY | NEXT: %s", jumpsRemaining, totalDist, nextJumpSystem)
	if jumpsRemaining == 0 && currIdx >= 0 {
		d.StatusBadge = &display.StatusBadge{
			Text:  "ARRIVED",
			Color: display.ColorOK,
		}
	} else {
		d.StatusBadge = &display.StatusBadge{
			Text:  fmt.Sprintf("%d REMAINING", jumpsRemaining),
			Color: display.ColorOK,
		}
	}

	items := make([]display.Item, 0, len(route))
	for i, r := range route {
		it := display.Item{
			ID:         fmt.Sprintf("nav_%d", i),
			Label:      r.StarSystem,
			Value:      r.StarClass,
			Icon:       StarClassIcon(r.StarClass),
			Action:     display.ActionClipboard,
			ValueColor: display.ColorNormal,
		}

		if strings.EqualFold(r.StarSystem, e.currentSystem) {
			it.Badge = "HERE"
		} else if currIdx >= 0 && i < currIdx {
			it.ValueColor = display.ColorDim
		}

		items = append(items, it)
	}

	return display.Tab{
		ID:    "navroute",
		Title: "NAVROUTE",
		Type:  display.TabTypeList,
		Items: items,
	}
}

func (e *RouteEngine) buildSpanshTabLocked() display.Tab {
	if len(e.spanshWaypoints) == 0 {
		return display.Tab{
			ID:    "spansh",
			Title: "SPANSH",
			Type:  display.TabTypeText,
			Text:  "SPANSH ROUTER\nExport CSV at spansh.co.uk\nSave as packages/route/route.csv\nWaypoints tracked on arrival.",
		}
	}

	items := make([]display.Item, 0, len(e.spanshWaypoints)+1)

	// Toggle row for auto_copy
	autoCopyVal := "OFF"
	autoCopyCol := display.ColorDim
	if e.autoCopy {
		autoCopyVal = "ON"
		autoCopyCol = display.ColorOK
	}

	items = append(items, display.Item{
		ID:         "auto_copy",
		Label:      "Auto-Copy Waypoint",
		Sublabel:   "Copies next system to clipboard on arrival",
		Value:      autoCopyVal,
		ValueColor: autoCopyCol,
		Action:     display.ActionEvent,
	})

	for i, wp := range e.spanshWaypoints {
		it := display.Item{
			ID:         fmt.Sprintf("spansh_%d", i),
			Label:      wp.SystemName,
			Sublabel:   fmt.Sprintf("%.1f LY | %d jumps", wp.DistanceRemaining, wp.Jumps),
			Action:     display.ActionClipboard,
			ValueColor: display.ColorNormal,
		}
		if wp.NeutronStar {
			it.Icon = display.IconNeutron
		}

		if strings.EqualFold(wp.SystemName, e.currentSystem) {
			it.Badge = "HERE"
		} else if e.currSpanshIdx >= 0 && i < e.currSpanshIdx {
			it.ValueColor = display.ColorDim
		}

		items = append(items, it)
	}

	return display.Tab{
		ID:    "spansh",
		Title: "SPANSH",
		Type:  display.TabTypeList,
		Items: items,
	}
}

func (e *RouteEngine) syncScreenLocked() {
	d := e.buildDisplayLocked()
	if e.onWriteHook != nil {
		e.onWriteHook(d)
	}
	if e.writer != nil && e.destPath != "" {
		_ = e.writer.Queue(e.destPath, d)
	}
}
