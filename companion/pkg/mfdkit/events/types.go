package events

import (
	"encoding/json"
	"time"
)

// EventType represents the type of MFD event.
type EventType = string

const (
	TypePluginStart      EventType = "plugin_start"
	TypeScreenLoaded     EventType = "screen_loaded"
	TypeLoadError        EventType = "load_error"
	TypeContextChanged   EventType = "context_changed"
	TypeFocusGained      EventType = "focus_gained"
	TypeFocusLost        EventType = "focus_lost"
	TypeTabChange        EventType = "tab_change"
	TypeNav              EventType = "nav"
	TypeSelect           EventType = "select"
	TypeAdjust           EventType = "adjust"
	TypeCompanionStarted EventType = "companion_started"
	TypeCompanionCrashed EventType = "companion_crashed"
	TypeCompanionGaveUp  EventType = "companion_gave_up"
	TypeEventsDropped    EventType = "events_dropped"
)

// ErrorDetails represents error-specific information emitted on load_error.
type ErrorDetails struct {
	File    string `json:"file,omitempty"`
	Line    int    `json:"line,omitempty"`
	Column  int    `json:"column,omitempty"`
	Message string `json:"message,omitempty"`
}

// ContextDetails represents game-context information emitted on context_changed.
type ContextDetails struct {
	Contexts []string `json:"contexts,omitempty"`
	Flags    uint32   `json:"flags,omitempty"`
	Flags2   uint32   `json:"flags2,omitempty"`
	GUIFocus int      `json:"guiFocus,omitempty"`
}

// CompanionDetails represents companion lifecycle metadata emitted on companion_* events.
type CompanionDetails struct {
	Companion string `json:"companion,omitempty"`
	PID       int    `json:"pid,omitempty"`
	ExitCode  int    `json:"exitCode,omitempty"`
	Attempt   int    `json:"attempt,omitempty"`
}

// Event represents a single record in events.jsonl per §6.2.
type Event struct {
	Seq     uint64    `json:"seq"`
	TS      time.Time `json:"ts"`
	Session string    `json:"session"`
	Screen  string    `json:"screen,omitempty"`
	Type    string    `json:"type"`
	Tab     string    `json:"tab,omitempty"`
	Index   int       `json:"index,omitempty"`
	ItemID  string    `json:"itemId,omitempty"`
	Label   string    `json:"label,omitempty"`
	Key     *string   `json:"key,omitempty"`
	Delta   *int      `json:"delta,omitempty"`
	Action  string    `json:"action,omitempty"`

	// Count is populated for events_dropped records.
	Count int `json:"count,omitempty"`

	// Structured details (can be unmarshaled from nested objects or flat fields).
	ErrorDetails     *ErrorDetails     `json:"errorDetails,omitempty"`
	ContextDetails   *ContextDetails   `json:"contextDetails,omitempty"`
	CompanionDetails *CompanionDetails `json:"companionDetails,omitempty"`

	// Raw is the original JSON representation of the event line.
	Raw json.RawMessage `json:"-"`
}

// UnmarshalJSON unmarshals Event supporting both flat top-level fields (per §6.2)
// and structured details objects.
func (e *Event) UnmarshalJSON(data []byte) error {
	type Alias Event
	var aux struct {
		Alias

		// Flat error fields
		File    string `json:"file"`
		Line    int    `json:"line"`
		Column  int    `json:"column"`
		Message string `json:"message"`

		// Flat context fields
		Contexts []string `json:"contexts"`
		Flags    uint32   `json:"flags"`
		Flags2   uint32   `json:"flags2"`
		GUIFocus int      `json:"guiFocus"`

		// Flat companion fields
		Companion string `json:"companion"`
		PID       int    `json:"pid"`
		ExitCode  int    `json:"exitCode"`
		Attempt   int    `json:"attempt"`

		// Flat events_dropped
		DropCount int `json:"dropCount"`
	}

	if err := json.Unmarshal(data, &aux); err != nil {
		return err
	}

	*e = Event(aux.Alias)
	e.Raw = append([]byte(nil), data...)

	// Populate ErrorDetails if not already populated from nested errorDetails
	if e.ErrorDetails == nil && (aux.File != "" || aux.Line != 0 || aux.Column != 0 || aux.Message != "") {
		e.ErrorDetails = &ErrorDetails{
			File:    aux.File,
			Line:    aux.Line,
			Column:  aux.Column,
			Message: aux.Message,
		}
	}

	// Populate ContextDetails if not already populated from nested contextDetails
	if e.ContextDetails == nil && (len(aux.Contexts) > 0 || aux.Flags != 0 || aux.Flags2 != 0 || aux.GUIFocus != 0) {
		e.ContextDetails = &ContextDetails{
			Contexts: aux.Contexts,
			Flags:    aux.Flags,
			Flags2:   aux.Flags2,
			GUIFocus: aux.GUIFocus,
		}
	}

	// Populate CompanionDetails if not already populated from nested companionDetails
	if e.CompanionDetails == nil && (aux.Companion != "" || aux.PID != 0 || aux.ExitCode != 0 || aux.Attempt != 0) {
		e.CompanionDetails = &CompanionDetails{
			Companion: aux.Companion,
			PID:       aux.PID,
			ExitCode:  aux.ExitCode,
			Attempt:   aux.Attempt,
		}
	}

	if e.Count == 0 && aux.DropCount != 0 {
		e.Count = aux.DropCount
	}

	return nil
}

// MarshalJSON marshals Event serializing detail fields flattened per §6.2.
func (e Event) MarshalJSON() ([]byte, error) {
	type Alias Event
	aux := struct {
		Alias
		// Flat error fields
		File    string `json:"file,omitempty"`
		Line    int    `json:"line,omitempty"`
		Column  int    `json:"column,omitempty"`
		Message string `json:"message,omitempty"`

		// Flat context fields
		Contexts []string `json:"contexts,omitempty"`
		Flags    *uint32  `json:"flags,omitempty"`
		Flags2   *uint32  `json:"flags2,omitempty"`
		GUIFocus *int     `json:"guiFocus,omitempty"`

		// Flat companion fields
		Companion string `json:"companion,omitempty"`
		PID       *int   `json:"pid,omitempty"`
		ExitCode  *int   `json:"exitCode,omitempty"`
		Attempt   *int   `json:"attempt,omitempty"`
	}{
		Alias: Alias(e),
	}

	// Suppress nesting in JSON output if we are flattening them
	aux.Alias.ErrorDetails = nil
	aux.Alias.ContextDetails = nil
	aux.Alias.CompanionDetails = nil

	if e.ErrorDetails != nil {
		aux.File = e.ErrorDetails.File
		aux.Line = e.ErrorDetails.Line
		aux.Column = e.ErrorDetails.Column
		aux.Message = e.ErrorDetails.Message
	}
	if e.ContextDetails != nil {
		aux.Contexts = e.ContextDetails.Contexts
		aux.Flags = &e.ContextDetails.Flags
		aux.Flags2 = &e.ContextDetails.Flags2
		aux.GUIFocus = &e.ContextDetails.GUIFocus
	}
	if e.CompanionDetails != nil {
		aux.Companion = e.CompanionDetails.Companion
		aux.PID = &e.CompanionDetails.PID
		aux.ExitCode = &e.CompanionDetails.ExitCode
		aux.Attempt = &e.CompanionDetails.Attempt
	}

	return json.Marshal(aux)
}
