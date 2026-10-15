//go:build !windows

package main

func querySteamInstallDirs() []string {
	return nil
}

func queryFrontierInstallDirs() []string {
	return nil
}
