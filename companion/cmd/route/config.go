package main

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"strconv"
	"strings"
)

// RouteConfig stores persistent configuration for the route companion (§12.3).
type RouteConfig struct {
	AutoCopy bool
	RouteCSV string
}

// LoadRouteINI reads the optional route.ini from iniPath.
func LoadRouteINI(iniPath string) RouteConfig {
	cfg := RouteConfig{
		AutoCopy: false,
		RouteCSV: "",
	}

	f, err := os.Open(iniPath)
	if err != nil {
		return cfg
	}
	defer f.Close()

	scanner := bufio.NewScanner(f)
	inRouteSection := false

	for scanner.Scan() {
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") || strings.HasPrefix(line, ";") {
			continue
		}

		if strings.HasPrefix(line, "[") && strings.HasSuffix(line, "]") {
			sec := strings.ToLower(strings.Trim(line, "[] \t"))
			inRouteSection = (sec == "route")
			continue
		}

		if !inRouteSection {
			continue
		}

		parts := strings.SplitN(line, "=", 2)
		if len(parts) != 2 {
			continue
		}

		key := strings.ToLower(strings.TrimSpace(parts[0]))
		val := strings.TrimSpace(parts[1])

		switch key {
		case "auto_copy", "autocopy":
			if b, err := strconv.ParseBool(val); err == nil {
				cfg.AutoCopy = b
			}
		case "route_csv", "routecsv", "csv_path":
			cfg.RouteCSV = val
		}
	}

	return cfg
}

// SaveRouteINI saves RouteConfig to iniPath.
func SaveRouteINI(iniPath string, cfg RouteConfig) error {
	dir := filepath.Dir(iniPath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}

	content := fmt.Sprintf("[route]\nauto_copy=%t\n", cfg.AutoCopy)
	if cfg.RouteCSV != "" {
		content += fmt.Sprintf("route_csv=%s\n", cfg.RouteCSV)
	}

	tmpPath := iniPath + ".tmp"
	if err := os.WriteFile(tmpPath, []byte(content), 0644); err != nil {
		return err
	}

	return os.Rename(tmpPath, iniPath)
}
