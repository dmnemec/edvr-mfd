//go:build windows

package main

import (
	"errors"
	"fmt"
	"syscall"
	"time"
	"unsafe"
)

var (
	user32DLL           = syscall.NewLazyDLL("user32.dll")
	kernel32DLL         = syscall.NewLazyDLL("kernel32.dll")
	procOpenClipboard   = user32DLL.NewProc("OpenClipboard")
	procCloseClipboard  = user32DLL.NewProc("CloseClipboard")
	procEmptyClipboard  = user32DLL.NewProc("EmptyClipboard")
	procSetClipboard    = user32DLL.NewProc("SetClipboardData")
	procGlobalAlloc     = kernel32DLL.NewProc("GlobalAlloc")
	procGlobalLock      = kernel32DLL.NewProc("GlobalLock")
	procGlobalUnlock    = kernel32DLL.NewProc("GlobalUnlock")
)

const (
	cfUnicodeText = 13
	gmemMoveable  = 0x0002
)

type win32Clipboard struct{}

func newOSClipboard() Clipboard {
	return &win32Clipboard{}
}

func (w *win32Clipboard) SetText(text string) error {
	utf16, err := syscall.UTF16FromString(text)
	if err != nil {
		return fmt.Errorf("UTF16 encoding error: %w", err)
	}

	byteLen := uintptr(len(utf16) * 2)

	// Clipboard may be temporarily locked by another process; retry briefly
	var opened bool
	for i := 0; i < 5; i++ {
		r, _, _ := procOpenClipboard.Call(0)
		if r != 0 {
			opened = true
			break
		}
		time.Sleep(10 * time.Millisecond)
	}
	if !opened {
		return errors.New("failed to open Windows clipboard")
	}
	defer procCloseClipboard.Call()

	procEmptyClipboard.Call()

	hMem, _, _ := procGlobalAlloc.Call(gmemMoveable, byteLen)
	if hMem == 0 {
		return errors.New("failed to allocate global memory for clipboard")
	}

	ptr, _, _ := procGlobalLock.Call(hMem)
	if ptr == 0 {
		return errors.New("failed to lock global memory for clipboard")
	}

	// Copy UTF-16 bytes into locked memory
	srcBytes := unsafe.Slice((*byte)(unsafe.Pointer(&utf16[0])), byteLen)
	dstBytes := unsafe.Slice((*byte)(unsafe.Pointer(ptr)), byteLen)
	copy(dstBytes, srcBytes)

	procGlobalUnlock.Call(hMem)

	res, _, errSet := procSetClipboard.Call(cfUnicodeText, hMem)
	if res == 0 {
		return fmt.Errorf("SetClipboardData failed: %v", errSet)
	}

	return nil
}
