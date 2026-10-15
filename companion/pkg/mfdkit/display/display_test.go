package display_test

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"testing"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/display"
)

const spec53SampleJSON = `{
  "schemaVersion": 1,
  "title": "MODULES",
  "subtitle": "INTEGRITY",
  "statusBadge": { "text": "2 DAMAGED", "color": "warn" },
  "footerHint": "SELECT: DETAILS",
  "tabs": [
    {
      "id": "integrity",
      "title": "INTEGRITY",
      "type": "list",
      "items": [
        {
          "id": "slot_fsd",
          "label": "Frame Shift Drive",
          "sublabel": "5A",
          "value": "87%",
          "valueColor": "warn",
          "icon": "warning",
          "checks": [ { "label": "S", "state": "done" }, { "label": "M", "state": "todo" } ],
          "action": "event"
        }
      ]
    },
    {
      "id": "pips",
      "title": "POWER",
      "type": "bars",
      "items": [ { "id": "sys", "label": "SYS", "value": 4, "max": 8, "segments": 8, "color": "accent" } ]
    },
    { "id": "kv", "title": "INFO", "type": "keyvalue", "items": [ { "id": "hull", "key": "Hull", "value": "100%", "valueColor": "ok" } ] },
    { "id": "about", "title": "ABOUT", "type": "text", "text": "Multi-line\ntext" }
  ]
}`

func TestSpec53Deserialization(t *testing.T) {
	var d display.Display
	err := json.Unmarshal([]byte(spec53SampleJSON), &d)
	if err != nil {
		t.Fatalf("Failed to unmarshal spec §5.3 sample JSON: %v", err)
	}

	if d.SchemaVersion != 1 {
		t.Errorf("Expected schemaVersion 1, got %d", d.SchemaVersion)
	}
	if d.Title != "MODULES" {
		t.Errorf("Expected title 'MODULES', got %q", d.Title)
	}
	if d.Subtitle != "INTEGRITY" {
		t.Errorf("Expected subtitle 'INTEGRITY', got %q", d.Subtitle)
	}
	if d.StatusBadge == nil || d.StatusBadge.Text != "2 DAMAGED" || d.StatusBadge.Color != "warn" {
		t.Errorf("Unexpected status badge: %+v", d.StatusBadge)
	}
	if d.FooterHint != "SELECT: DETAILS" {
		t.Errorf("Expected footerHint 'SELECT: DETAILS', got %q", d.FooterHint)
	}
	if len(d.Tabs) != 4 {
		t.Fatalf("Expected 4 tabs, got %d", len(d.Tabs))
	}

	// Tab 0: list
	t0 := d.Tabs[0]
	if t0.ID != "integrity" || t0.Type != display.TabTypeList || len(t0.Items) != 1 {
		t.Errorf("Tab 0 mismatch: %+v", t0)
	} else {
		item := t0.Items[0]
		if item.ID != "slot_fsd" || item.Label != "Frame Shift Drive" || item.Sublabel != "5A" ||
			item.Value != "87%" || item.ValueColor != display.ColorWarn || item.Icon != display.IconWarning ||
			item.Action != display.ActionEvent || len(item.Checks) != 2 {
			t.Errorf("Tab 0 item mismatch: %+v", item)
		}
		if item.Checks[0].Label != "S" || item.Checks[0].State != display.CheckStateDone {
			t.Errorf("Tab 0 check 0 mismatch: %+v", item.Checks[0])
		}
		if item.Checks[1].Label != "M" || item.Checks[1].State != display.CheckStateTodo {
			t.Errorf("Tab 0 check 1 mismatch: %+v", item.Checks[1])
		}
	}

	// Tab 1: bars
	t1 := d.Tabs[1]
	if t1.ID != "pips" || t1.Type != display.TabTypeBars || len(t1.Bars) != 1 {
		t.Errorf("Tab 1 mismatch: %+v", t1)
	} else {
		bar := t1.Bars[0]
		if bar.ID != "sys" || bar.Label != "SYS" || bar.Value != 4 || bar.Max != 8 || bar.Segments != 8 || bar.Color != display.ColorAccent {
			t.Errorf("Tab 1 bar mismatch: %+v", bar)
		}
	}

	// Tab 2: keyvalue
	t2 := d.Tabs[2]
	if t2.ID != "kv" || t2.Type != display.TabTypeKeyValue || len(t2.KeyValues) != 1 {
		t.Errorf("Tab 2 mismatch: %+v", t2)
	} else {
		kv := t2.KeyValues[0]
		if kv.ID != "hull" || kv.Key != "Hull" || kv.Value != "100%" || kv.ValueColor != display.ColorOK {
			t.Errorf("Tab 2 keyvalue mismatch: %+v", kv)
		}
	}

	// Tab 3: text
	t3 := d.Tabs[3]
	if t3.ID != "about" || t3.Type != display.TabTypeText || t3.Text != "Multi-line\ntext" {
		t.Errorf("Tab 3 mismatch: %+v", t3)
	}

	// Validate should pass
	if err := display.Validate(&d); err != nil {
		t.Errorf("Validation failed on spec §5.3 sample: %v", err)
	}
}

func TestStatusBadgeStringDeserialization(t *testing.T) {
	jsonStr := `{
		"schemaVersion": 1,
		"title": "TEST",
		"statusBadge": "ONLINE",
		"tabs": [
			{"id": "t1", "title": "T1", "type": "list"}
		]
	}`

	var d display.Display
	if err := json.Unmarshal([]byte(jsonStr), &d); err != nil {
		t.Fatalf("Failed to unmarshal string statusBadge: %v", err)
	}
	if d.StatusBadge == nil || d.StatusBadge.Text != "ONLINE" || d.StatusBadge.Color != display.ColorOK {
		t.Errorf("Unexpected string badge parse: %+v", d.StatusBadge)
	}
	if err := display.Validate(&d); err != nil {
		t.Errorf("Expected valid display, got %v", err)
	}
}

func TestSerializationRoundTrip(t *testing.T) {
	orig := display.Display{
		SchemaVersion: 1,
		Title:         "ROUNDTRIP",
		Subtitle:      "TEST",
		StatusBadge: &display.StatusBadge{
			Text:  "READY",
			Color: display.PaletteOK,
		},
		FooterHint: "PRESS SELECT",
		Tabs: []display.Tab{
			{
				ID:    "list_tab",
				Title: "LIST",
				Type:  display.TabTypeList,
				Items: []display.Item{
					{
						ID:         "i1",
						Label:      "Primary Engine",
						Sublabel:   "Clean Tuning",
						Value:      "100%",
						Badge:      "A-RATED",
						ValueColor: display.PaletteOK,
						Icon:       display.IconCheck,
						Checks: []display.Check{
							{Label: "S", State: display.CheckStateDone},
						},
						Action: display.ActionClipboard,
					},
				},
			},
			{
				ID:    "bars_tab",
				Title: "BARS",
				Type:  display.TabTypeBars,
				Bars: []display.Bar{
					{
						ID:       "sys_pips",
						Label:    "SYS",
						Value:    3.5,
						Max:      8,
						Segments: 8,
						Color:    display.PaletteAccent,
					},
				},
			},
			{
				ID:    "kv_tab",
				Title: "KV",
				Type:  display.TabTypeKeyValue,
				KeyValues: []display.KeyValue{
					{
						ID:         "hull_kv",
						Key:        "Hull",
						Value:      "100%",
						ValueColor: display.PaletteOK,
					},
				},
			},
			{
				ID:    "txt_tab",
				Title: "TXT",
				Type:  display.TabTypeText,
				Text:  "Status Log\nLine 2",
			},
		},
	}

	if err := display.Validate(&orig); err != nil {
		t.Fatalf("Original display validation failed: %v", err)
	}

	data, err := json.Marshal(orig)
	if err != nil {
		t.Fatalf("Marshal failed: %v", err)
	}

	var roundtrip display.Display
	if err := json.Unmarshal(data, &roundtrip); err != nil {
		t.Fatalf("Unmarshal failed: %v", err)
	}

	if err := display.Validate(&roundtrip); err != nil {
		t.Fatalf("Roundtrip validation failed: %v", err)
	}

	if roundtrip.Title != orig.Title || len(roundtrip.Tabs) != len(orig.Tabs) {
		t.Errorf("Roundtrip mismatch: %+v vs %+v", roundtrip, orig)
	}
	if len(roundtrip.Tabs[0].Items) != 1 || roundtrip.Tabs[0].Items[0].ID != "i1" {
		t.Errorf("Roundtrip list item mismatch: %+v", roundtrip.Tabs[0].Items)
	}
	if len(roundtrip.Tabs[1].Bars) != 1 || roundtrip.Tabs[1].Bars[0].ID != "sys_pips" {
		t.Errorf("Roundtrip bar mismatch: %+v", roundtrip.Tabs[1].Bars)
	}
	if len(roundtrip.Tabs[2].KeyValues) != 1 || roundtrip.Tabs[2].KeyValues[0].ID != "hull_kv" {
		t.Errorf("Roundtrip keyvalue mismatch: %+v", roundtrip.Tabs[2].KeyValues)
	}
}

func TestValidationErrors(t *testing.T) {
	validDisplay := func() display.Display {
		return display.Display{
			SchemaVersion: 1,
			Title:         "TEST DISPLAY",
			Tabs: []display.Tab{
				{
					ID:    "tab1",
					Title: "TAB 1",
					Type:  display.TabTypeList,
					Items: []display.Item{
						{ID: "item1", Label: "Item 1"},
					},
				},
			},
		}
	}

	t.Run("nil display", func(t *testing.T) {
		err := display.Validate(nil)
		if err == nil {
			t.Error("Expected error for nil display, got nil")
		}
	})

	t.Run("schemaVersion != 1", func(t *testing.T) {
		d := validDisplay()
		d.SchemaVersion = 2
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "schemaVersion") {
			t.Errorf("Expected schemaVersion error, got %v", err)
		}

		d.SchemaVersion = 0
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "schemaVersion") {
			t.Errorf("Expected schemaVersion error, got %v", err)
		}
	})

	t.Run("forbidden tab ID __settings", func(t *testing.T) {
		d := validDisplay()
		d.Tabs[0].ID = display.ForbiddenTabID
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "__settings") {
			t.Errorf("Expected forbidden tab ID error, got %v", err)
		}
	})

	t.Run("empty tab ID", func(t *testing.T) {
		d := validDisplay()
		d.Tabs[0].ID = ""
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "empty id") {
			t.Errorf("Expected empty tab id error, got %v", err)
		}
	})

	t.Run("duplicate tab ID", func(t *testing.T) {
		d := validDisplay()
		d.Tabs = append(d.Tabs, display.Tab{
			ID:    "tab1",
			Title: "Duplicate Tab",
			Type:  display.TabTypeList,
		})
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "duplicate tab id") {
			t.Errorf("Expected duplicate tab id error, got %v", err)
		}
	})

	t.Run("more than 16 tabs", func(t *testing.T) {
		d := validDisplay()
		d.Tabs = make([]display.Tab, 17)
		for i := 0; i < 17; i++ {
			d.Tabs[i] = display.Tab{
				ID:    strings.Repeat("a", i+1),
				Title: "Tab",
				Type:  display.TabTypeList,
			}
		}
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "maximum limit") {
			t.Errorf("Expected tab limit error, got %v", err)
		}
	})

	t.Run("empty item ID in list tab", func(t *testing.T) {
		d := validDisplay()
		d.Tabs[0].Items[0].ID = ""
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "empty id") {
			t.Errorf("Expected empty item id error, got %v", err)
		}
	})

	t.Run("duplicate item ID in list tab", func(t *testing.T) {
		d := validDisplay()
		d.Tabs[0].Items = append(d.Tabs[0].Items, display.Item{ID: "item1", Label: "Duplicate"})
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "duplicate item id") {
			t.Errorf("Expected duplicate item id error, got %v", err)
		}
	})

	t.Run("duplicate item ID across keyvalue", func(t *testing.T) {
		d := validDisplay()
		d.Tabs[0].Type = display.TabTypeKeyValue
		d.Tabs[0].Items = nil
		d.Tabs[0].KeyValues = []display.KeyValue{
			{ID: "kv1", Key: "K1", Value: "V1"},
			{ID: "kv1", Key: "K2", Value: "V2"},
		}
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "duplicate item id") {
			t.Errorf("Expected duplicate kv item id error, got %v", err)
		}
	})

	t.Run("duplicate bar ID in bars tab", func(t *testing.T) {
		d := validDisplay()
		d.Tabs[0].Type = display.TabTypeBars
		d.Tabs[0].Items = nil
		d.Tabs[0].Bars = []display.Bar{
			{ID: "bar1", Label: "B1", Value: 1, Max: 2},
			{ID: "bar1", Label: "B2", Value: 2, Max: 4},
		}
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "duplicate item id") {
			t.Errorf("Expected duplicate bar id error, got %v", err)
		}
	})

	t.Run("more than 256 items in tab", func(t *testing.T) {
		d := validDisplay()
		d.Tabs[0].Items = make([]display.Item, 257)
		for i := 0; i < 257; i++ {
			d.Tabs[0].Items[i] = display.Item{
				ID:    strings.Repeat("b", i+1),
				Label: "Item",
			}
		}
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "exceeding limit of 256") {
			t.Errorf("Expected item limit error, got %v", err)
		}
	})

	t.Run("string length > 128", func(t *testing.T) {
		longStr := strings.Repeat("x", 129)

		d := validDisplay()
		d.Title = longStr
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "exceeds limit of 128") {
			t.Errorf("Expected title length error, got %v", err)
		}

		d = validDisplay()
		d.Tabs[0].Items[0].Label = longStr
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "exceeds limit of 128") {
			t.Errorf("Expected item label length error, got %v", err)
		}

		d = validDisplay()
		d.Tabs[0].ID = longStr
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "exceeds limit of 128") {
			t.Errorf("Expected tab id length error, got %v", err)
		}
	})

	t.Run("more than 6 checks per item", func(t *testing.T) {
		d := validDisplay()
		d.Tabs[0].Items[0].Checks = make([]display.Check, 7)
		for i := 0; i < 7; i++ {
			d.Tabs[0].Items[0].Checks[i] = display.Check{Label: "C", State: display.CheckStateDone}
		}
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "check limit of 6") {
			t.Errorf("Expected check limit error, got %v", err)
		}
	})

	t.Run("invalid tab type", func(t *testing.T) {
		d := validDisplay()
		d.Tabs[0].Type = "unsupported_type"
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "unknown type") {
			t.Errorf("Expected unknown tab type error, got %v", err)
		}

		d.Tabs[0].Type = ""
		if err := display.Validate(&d); err == nil || !strings.Contains(err.Error(), "empty type") {
			t.Errorf("Expected empty tab type error, got %v", err)
		}
	})
}

func TestJSONSchemasAreValidJSON(t *testing.T) {
	manifestSchemaPath := filepath.Join("..", "..", "..", "..", "docs", "schema", "manifest.v1.schema.json")
	displaySchemaPath := filepath.Join("..", "..", "..", "..", "docs", "schema", "display.v1.schema.json")

	manifestData, err := os.ReadFile(manifestSchemaPath)
	if err != nil {
		t.Fatalf("Failed to read manifest schema file: %v", err)
	}
	var manifestSchema map[string]any
	if err := json.Unmarshal(manifestData, &manifestSchema); err != nil {
		t.Fatalf("Manifest schema is not valid JSON: %v", err)
	}
	if manifestSchema["$schema"] == nil || manifestSchema["title"] == nil {
		t.Errorf("Manifest schema missing required schema metadata")
	}

	displayData, err := os.ReadFile(displaySchemaPath)
	if err != nil {
		t.Fatalf("Failed to read display schema file: %v", err)
	}
	var displaySchema map[string]any
	if err := json.Unmarshal(displayData, &displaySchema); err != nil {
		t.Fatalf("Display schema is not valid JSON: %v", err)
	}
	if displaySchema["$schema"] == nil || displaySchema["title"] == nil {
		t.Errorf("Display schema missing required schema metadata")
	}
}
