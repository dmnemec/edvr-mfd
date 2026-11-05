//go:build !windows

package events

import "os"

func openSharedRead(path string) (*os.File, error) {
	return os.Open(path)
}

