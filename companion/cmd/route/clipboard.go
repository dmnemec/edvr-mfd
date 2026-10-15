package main

// Clipboard abstracts system clipboard interactions for testing and multi-platform support.
type Clipboard interface {
	SetText(text string) error
}

var defaultClipboard Clipboard = newOSClipboard()

// MockClipboard records copied text in memory for tests.
type MockClipboard struct {
	LastCopied string
	AllCopied  []string
}

func (m *MockClipboard) SetText(text string) error {
	m.LastCopied = text
	m.AllCopied = append(m.AllCopied, text)
	return nil
}
