//go:build !windows

package env

// AcquireSingleInstanceMutex is a stub for non-Windows platforms (e.g. testing under Linux/WSL).
func AcquireSingleInstanceMutex(name string) (func(), bool, error) {
	return func() {}, true, nil
}
