//go:build !windows

package main

type stubClipboard struct {
	lastText string
}

func newOSClipboard() Clipboard {
	return &stubClipboard{}
}

func (s *stubClipboard) SetText(text string) error {
	s.lastText = text
	return nil
}
