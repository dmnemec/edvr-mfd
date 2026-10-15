//go:build windows

package env

import (
	"syscall"
	"unsafe"
)

var (
	kernel32        = syscall.NewLazyDLL("kernel32.dll")
	procCreateMutex = kernel32.NewProc("CreateMutexW")
	procCloseHandle = kernel32.NewProc("CloseHandle")
)

const errorAlreadyExists = 183

// AcquireSingleInstanceMutex attempts to acquire a named Windows mutex.
// Returns (releaseFn, true, nil) if successfully acquired as first instance.
// Returns (nil, false, nil) if mutex already exists (another instance running).
func AcquireSingleInstanceMutex(name string) (func(), bool, error) {
	namePtr, err := syscall.UTF16PtrFromString(name)
	if err != nil {
		return nil, false, err
	}

	h, _, callErr := procCreateMutex.Call(0, 0, uintptr(unsafe.Pointer(namePtr)))
	if h == 0 {
		return nil, false, callErr
	}

	errno, ok := callErr.(syscall.Errno)
	if ok && errno == errorAlreadyExists {
		procCloseHandle.Call(h)
		return nil, false, nil
	}

	cleanup := func() {
		procCloseHandle.Call(h)
	}

	return cleanup, true, nil
}
