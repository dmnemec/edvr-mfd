package main

import (
	"syscall"
	"unsafe"
)

var (
	modadvapi32         = syscall.NewLazyDLL("advapi32.dll")
	procRegOpenKeyExW    = modadvapi32.NewProc("RegOpenKeyExW")
	procRegQueryValueExW = modadvapi32.NewProc("RegQueryValueExW")
	procRegCloseKey      = modadvapi32.NewProc("RegCloseKey")
	procRegEnumKeyExW    = modadvapi32.NewProc("RegEnumKeyExW")
)

const (
	hkeyLocalMachine = 0x80000002
	hkeyCurrentUser  = 0x80000001
	keyQueryValue    = 0x0001
	keyEnumerateSub  = 0x0008
	keyWow6432Key    = 0x0200
)

func regGetString(root uintptr, subKey string, valueName string, extraFlags uint32) (string, error) {
	subKeyUTF16, err := syscall.UTF16PtrFromString(subKey)
	if err != nil {
		return "", err
	}
	valNameUTF16, err := syscall.UTF16PtrFromString(valueName)
	if err != nil {
		return "", err
	}

	var hKey uintptr
	flags := uint32(keyQueryValue) | extraFlags
	ret, _, _ := procRegOpenKeyExW.Call(root, uintptr(unsafe.Pointer(subKeyUTF16)), 0, uintptr(flags), uintptr(unsafe.Pointer(&hKey)))
	if ret != 0 {
		return "", syscall.Errno(ret)
	}
	defer procRegCloseKey.Call(hKey)

	var valType uint32
	var bufSize uint32
	ret, _, _ = procRegQueryValueExW.Call(hKey, uintptr(unsafe.Pointer(valNameUTF16)), 0, uintptr(unsafe.Pointer(&valType)), 0, uintptr(unsafe.Pointer(&bufSize)))
	if ret != 0 {
		return "", syscall.Errno(ret)
	}

	buf := make([]uint16, (bufSize/2)+2)
	ret, _, _ = procRegQueryValueExW.Call(hKey, uintptr(unsafe.Pointer(valNameUTF16)), 0, uintptr(unsafe.Pointer(&valType)), uintptr(unsafe.Pointer(&buf[0])), uintptr(unsafe.Pointer(&bufSize)))
	if ret != 0 {
		return "", syscall.Errno(ret)
	}

	return syscall.UTF16ToString(buf), nil
}

func querySteamInstallDirs() []string {
	var roots []string
	if p, err := regGetString(hkeyLocalMachine, `SOFTWARE\WOW6432Node\Valve\Steam`, "InstallPath", keyWow6432Key); err == nil && p != "" {
		roots = append(roots, p)
	}
	if p, err := regGetString(hkeyCurrentUser, `SOFTWARE\Valve\Steam`, "SteamPath", 0); err == nil && p != "" {
		roots = append(roots, p)
	}
	return roots
}

func queryFrontierInstallDirs() []string {
	var candidates []string
	roots := []uintptr{hkeyLocalMachine, hkeyCurrentUser}
	keys := []string{
		`SOFTWARE\Frontier Developments\Elite Dangerous`,
		`SOFTWARE\WOW6432Node\Frontier Developments\Elite Dangerous`,
		`SOFTWARE\Frontier Developments\EDLaunch`,
	}

	for _, r := range roots {
		for _, k := range keys {
			if p, err := regGetString(r, k, "InstallDir", 0); err == nil && p != "" {
				candidates = append(candidates, p)
			}
			if p, err := regGetString(r, k, "Path", 0); err == nil && p != "" {
				candidates = append(candidates, p)
			}
		}
	}
	return candidates
}
