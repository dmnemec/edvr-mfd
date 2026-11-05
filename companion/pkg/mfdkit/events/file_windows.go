package events

import (
	"os"
	"syscall"
)

// openSharedRead opens a file for reading with full share modes (FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE)
// so that Windows allows the file to be renamed, rolled, or deleted while open.
func openSharedRead(path string) (*os.File, error) {
	path16, err := syscall.UTF16PtrFromString(path)
	if err != nil {
		return nil, err
	}
	h, err := syscall.CreateFile(
		path16,
		syscall.GENERIC_READ,
		syscall.FILE_SHARE_READ|syscall.FILE_SHARE_WRITE|syscall.FILE_SHARE_DELETE,
		nil,
		syscall.OPEN_EXISTING,
		syscall.FILE_ATTRIBUTE_NORMAL,
		0,
	)
	if err != nil {
		return nil, err
	}
	return os.NewFile(uintptr(h), path), nil
}

