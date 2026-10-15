package display

import (
	"encoding/json"
	"errors"
	"fmt"
)

// Schema version constant (§5.3).
const CurrentSchemaVersion = 1

// Limit constants (§5.3).
const (
	MaxTabs           = 16
	MaxItemsPerTab    = 256
	MaxStringLength   = 128
	MaxChecksPerItem  = 6
	ForbiddenTabID    = "__settings"
)

// Named palette color constants (§5.3).
const (
	PaletteNormal = "normal"
	PaletteDim    = "dim"
	PaletteAccent = "accent"
	PaletteOK     = "ok"
	PaletteWarn   = "warn"
	PaletteCrit   = "crit"

	ColorNormal = PaletteNormal
	ColorDim    = PaletteDim
	ColorAccent = PaletteAccent
	ColorOK     = PaletteOK
	ColorWarn   = PaletteWarn
	ColorCrit   = PaletteCrit
)

// Icon glyph constants (§5.3).
const (
	IconFuel       = "fuel"
	IconWarning    = "warning"
	IconNeutron    = "neutron"
	IconWhiteDwarf = "whitedwarf"
	IconBio        = "bio"
	IconGeo        = "geo"
	IconCheck      = "check"
	IconLock       = "lock"
	IconStar       = "star"
)

// Tab type constants (§5.3).
const (
	TabTypeList     = "list"
	TabTypeKeyValue = "keyvalue"
	TabTypeText     = "text"
	TabTypeBars     = "bars"
)

// Check state constants (§5.3).
const (
	CheckStateDone = "done"
	CheckStateTodo = "todo"
	CheckStateNA   = "na"
)

// Action constants (§5.3).
const (
	ActionEvent     = "event"
	ActionClipboard = "clipboard"
	ActionDefault   = ActionEvent
)

// StatusBadge represents the status badge in the upper right header.
type StatusBadge struct {
	Text  string `json:"text"`
	Color string `json:"color,omitempty"`
}

// UnmarshalJSON supports both a string ("ONLINE") and an object ({"text":"...", "color":"..."}).
func (s *StatusBadge) UnmarshalJSON(data []byte) error {
	var str string
	if err := json.Unmarshal(data, &str); err == nil {
		s.Text = str
		s.Color = ColorOK
		return nil
	}

	type rawBadge StatusBadge
	var raw rawBadge
	if err := json.Unmarshal(data, &raw); err != nil {
		return err
	}
	*s = StatusBadge(raw)
	return nil
}

// Check represents an individual checklist toggle box on an item.
type Check struct {
	Label string `json:"label"` // 1-2 chars
	State string `json:"state"` // "done" | "todo" | "na"
}

// Item represents a row in a list tab.
type Item struct {
	ID         string  `json:"id"`
	Label      string  `json:"label,omitempty"`
	Sublabel   string  `json:"sublabel,omitempty"`
	Value      string  `json:"value,omitempty"`
	Badge      string  `json:"badge,omitempty"`
	ValueColor string  `json:"valueColor,omitempty"`
	Icon       string  `json:"icon,omitempty"`
	Checks     []Check `json:"checks,omitempty"`
	Action     string  `json:"action,omitempty"`
}

// KeyValue represents an entry in a key-value grid tab.
type KeyValue struct {
	ID         string `json:"id"`
	Key        string `json:"key"`
	Value      string `json:"value"`
	ValueColor string `json:"valueColor,omitempty"`
	Action     string `json:"action,omitempty"`
}

// Bar represents a progress/gauge bar in a bars tab.
type Bar struct {
	ID       string  `json:"id"`
	Label    string  `json:"label"`
	Value    float64 `json:"value"`
	Max      float64 `json:"max"`
	Segments int     `json:"segments,omitempty"`
	Color    string  `json:"color,omitempty"`
}

// Tab represents an individual display tab.
type Tab struct {
	ID        string     `json:"id"`
	Title     string     `json:"title"`
	Type      string     `json:"type"` // "list", "keyvalue", "text", "bars"
	Items     []Item     `json:"items,omitempty"`
	KeyValues []KeyValue `json:"keyvalues,omitempty"`
	Bars      []Bar      `json:"bars,omitempty"`
	Text      string     `json:"text,omitempty"`
}

// MarshalJSON serializes Tab, mapping KeyValues and Bars into "items" when appropriate (§5.3).
func (t Tab) MarshalJSON() ([]byte, error) {
	type TabAlias struct {
		ID        string     `json:"id"`
		Title     string     `json:"title"`
		Type      string     `json:"type"`
		Items     any        `json:"items,omitempty"`
		KeyValues []KeyValue `json:"keyvalues,omitempty"`
		Bars      []Bar      `json:"bars,omitempty"`
		Text      string     `json:"text,omitempty"`
	}

	alias := TabAlias{
		ID:    t.ID,
		Title: t.Title,
		Type:  t.Type,
		Text:  t.Text,
	}

	switch t.Type {
	case TabTypeBars:
		if len(t.Bars) > 0 {
			alias.Items = t.Bars
		} else if len(t.Items) > 0 {
			alias.Items = t.Items
		}
	case TabTypeKeyValue:
		if len(t.KeyValues) > 0 {
			alias.Items = t.KeyValues
		} else if len(t.Items) > 0 {
			alias.Items = t.Items
		}
	case TabTypeList:
		if len(t.Items) > 0 {
			alias.Items = t.Items
		}
	default:
		if len(t.Items) > 0 {
			alias.Items = t.Items
		} else if len(t.KeyValues) > 0 {
			alias.KeyValues = t.KeyValues
		} else if len(t.Bars) > 0 {
			alias.Bars = t.Bars
		}
	}

	return json.Marshal(alias)
}

// UnmarshalJSON unmarshals Tab, supporting typed items according to Tab.Type (§5.3).
func (t *Tab) UnmarshalJSON(data []byte) error {
	type rawTab struct {
		ID        string          `json:"id"`
		Title     string          `json:"title"`
		Type      string          `json:"type"`
		Items     json.RawMessage `json:"items,omitempty"`
		KeyValues []KeyValue      `json:"keyvalues,omitempty"`
		Bars      []Bar           `json:"bars,omitempty"`
		Text      string          `json:"text,omitempty"`
	}

	var raw rawTab
	if err := json.Unmarshal(data, &raw); err != nil {
		return err
	}

	t.ID = raw.ID
	t.Title = raw.Title
	t.Type = raw.Type
	t.Text = raw.Text
	t.KeyValues = raw.KeyValues
	t.Bars = raw.Bars

	if len(raw.Items) > 0 {
		switch t.Type {
		case TabTypeKeyValue:
			if len(t.KeyValues) == 0 {
				var kvs []KeyValue
				if err := json.Unmarshal(raw.Items, &kvs); err == nil {
					t.KeyValues = kvs
				}
			}
		case TabTypeBars:
			if len(t.Bars) == 0 {
				var bars []Bar
				if err := json.Unmarshal(raw.Items, &bars); err == nil {
					t.Bars = bars
				}
			}
		case TabTypeList:
			if len(t.Items) == 0 {
				var items []Item
				if err := json.Unmarshal(raw.Items, &items); err == nil {
					t.Items = items
				}
			}
		default:
			if len(t.Items) == 0 {
				var items []Item
				if err := json.Unmarshal(raw.Items, &items); err == nil {
					t.Items = items
				}
			}
		}
	}

	return nil
}

// Display represents the root display model matching Schema v1 (§5.3).
type Display struct {
	SchemaVersion int          `json:"schemaVersion"`
	Title         string       `json:"title"`
	Subtitle      string       `json:"subtitle,omitempty"`
	StatusBadge   *StatusBadge `json:"statusBadge,omitempty"`
	FooterHint    string       `json:"footerHint,omitempty"`
	Tabs          []Tab        `json:"tabs"`
}

// Validate validates a Display according to Schema v1 specification (§5.3):
// - schemaVersion must be 1
// - tab IDs and item IDs must not be empty and must be unique within their container
// - limits: <= 16 tabs, <= 256 items/tab, string lengths <= 128
// - forbidden tab ID: "__settings"
func Validate(d *Display) error {
	if d == nil {
		return errors.New("display cannot be nil")
	}

	if d.SchemaVersion != CurrentSchemaVersion {
		return fmt.Errorf("schemaVersion must be %d, got %d", CurrentSchemaVersion, d.SchemaVersion)
	}

	if err := validateStringLen("title", d.Title); err != nil {
		return err
	}
	if err := validateStringLen("subtitle", d.Subtitle); err != nil {
		return err
	}
	if err := validateStringLen("footerHint", d.FooterHint); err != nil {
		return err
	}
	if d.StatusBadge != nil {
		if err := validateStringLen("statusBadge.text", d.StatusBadge.Text); err != nil {
			return err
		}
		if err := validateStringLen("statusBadge.color", d.StatusBadge.Color); err != nil {
			return err
		}
	}

	if len(d.Tabs) > MaxTabs {
		return fmt.Errorf("tabs count %d exceeds maximum limit of %d", len(d.Tabs), MaxTabs)
	}

	seenTabIDs := make(map[string]bool, len(d.Tabs))

	for tIdx, tab := range d.Tabs {
		if tab.ID == "" {
			return fmt.Errorf("tab at index %d has empty id", tIdx)
		}
		if tab.ID == ForbiddenTabID {
			return fmt.Errorf("tab id %q is forbidden (reserved for system settings)", ForbiddenTabID)
		}
		if seenTabIDs[tab.ID] {
			return fmt.Errorf("duplicate tab id: %q", tab.ID)
		}
		seenTabIDs[tab.ID] = true

		if err := validateStringLen(fmt.Sprintf("tab[%q].id", tab.ID), tab.ID); err != nil {
			return err
		}
		if err := validateStringLen(fmt.Sprintf("tab[%q].title", tab.ID), tab.Title); err != nil {
			return err
		}
		if err := validateStringLen(fmt.Sprintf("tab[%q].text", tab.ID), tab.Text); err != nil {
			return err
		}

		switch tab.Type {
		case TabTypeList, TabTypeKeyValue, TabTypeText, TabTypeBars:
			// valid tab type
		case "":
			return fmt.Errorf("tab %q has empty type", tab.ID)
		default:
			return fmt.Errorf("tab %q has unknown type: %q", tab.ID, tab.Type)
		}

		totalItems := len(tab.Items) + len(tab.KeyValues) + len(tab.Bars)
		if totalItems > MaxItemsPerTab {
			return fmt.Errorf("tab %q has %d items, exceeding limit of %d", tab.ID, totalItems, MaxItemsPerTab)
		}

		seenItemIDs := make(map[string]bool, totalItems)

		for iIdx, item := range tab.Items {
			if item.ID == "" {
				return fmt.Errorf("tab %q item at index %d has empty id", tab.ID, iIdx)
			}
			if seenItemIDs[item.ID] {
				return fmt.Errorf("tab %q has duplicate item id: %q", tab.ID, item.ID)
			}
			seenItemIDs[item.ID] = true

			if err := validateStringLen(fmt.Sprintf("tab[%q].item[%q].id", tab.ID, item.ID), item.ID); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].item[%q].label", tab.ID, item.ID), item.Label); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].item[%q].sublabel", tab.ID, item.ID), item.Sublabel); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].item[%q].value", tab.ID, item.ID), item.Value); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].item[%q].badge", tab.ID, item.ID), item.Badge); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].item[%q].valueColor", tab.ID, item.ID), item.ValueColor); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].item[%q].icon", tab.ID, item.ID), item.Icon); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].item[%q].action", tab.ID, item.ID), item.Action); err != nil {
				return err
			}

			if len(item.Checks) > MaxChecksPerItem {
				return fmt.Errorf("tab %q item %q has %d checks, exceeding check limit of %d", tab.ID, item.ID, len(item.Checks), MaxChecksPerItem)
			}
			for cIdx, check := range item.Checks {
				if err := validateStringLen(fmt.Sprintf("tab[%q].item[%q].check[%d].label", tab.ID, item.ID, cIdx), check.Label); err != nil {
					return err
				}
				if err := validateStringLen(fmt.Sprintf("tab[%q].item[%q].check[%d].state", tab.ID, item.ID, cIdx), check.State); err != nil {
					return err
				}
			}
		}

		for kIdx, kv := range tab.KeyValues {
			if kv.ID == "" {
				return fmt.Errorf("tab %q keyvalue at index %d has empty id", tab.ID, kIdx)
			}
			if seenItemIDs[kv.ID] {
				return fmt.Errorf("tab %q has duplicate item id: %q", tab.ID, kv.ID)
			}
			seenItemIDs[kv.ID] = true

			if err := validateStringLen(fmt.Sprintf("tab[%q].kv[%q].id", tab.ID, kv.ID), kv.ID); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].kv[%q].key", tab.ID, kv.ID), kv.Key); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].kv[%q].value", tab.ID, kv.ID), kv.Value); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].kv[%q].valueColor", tab.ID, kv.ID), kv.ValueColor); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].kv[%q].action", tab.ID, kv.ID), kv.Action); err != nil {
				return err
			}
		}

		for bIdx, bar := range tab.Bars {
			if bar.ID == "" {
				return fmt.Errorf("tab %q bar at index %d has empty id", tab.ID, bIdx)
			}
			if seenItemIDs[bar.ID] {
				return fmt.Errorf("tab %q has duplicate item id: %q", tab.ID, bar.ID)
			}
			seenItemIDs[bar.ID] = true

			if err := validateStringLen(fmt.Sprintf("tab[%q].bar[%q].id", tab.ID, bar.ID), bar.ID); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].bar[%q].label", tab.ID, bar.Label), bar.Label); err != nil {
				return err
			}
			if err := validateStringLen(fmt.Sprintf("tab[%q].bar[%q].color", tab.ID, bar.ID), bar.Color); err != nil {
				return err
			}
		}
	}

	return nil
}

func validateStringLen(field, val string) error {
	if len(val) > MaxStringLength {
		return fmt.Errorf("%s length (%d) exceeds limit of %d", field, len(val), MaxStringLength)
	}
	return nil
}
