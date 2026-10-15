package main

import (
	"encoding/csv"
	"fmt"
	"io"
	"os"
	"strconv"
	"strings"
)

// SpanshWaypoint represents a waypoint exported from Spansh Neutron Plotter.
type SpanshWaypoint struct {
	SystemName        string
	DistanceToArrival float64
	DistanceRemaining float64
	NeutronStar       bool
	Jumps             int
}

// ParseSpanshCSV parses CSV data exported from Spansh Neutron Plotter.
// Expected columns (case-insensitive):
// System Name, Distance To Arrival, Distance Remaining, Neutron Star, Jumps
func ParseSpanshCSV(r io.Reader) ([]SpanshWaypoint, error) {
	reader := csv.NewReader(r)
	reader.TrimLeadingSpace = true
	reader.FieldsPerRecord = -1 // Allow variable fields if extra columns exist

	header, err := reader.Read()
	if err != nil {
		return nil, fmt.Errorf("failed to read CSV header: %w", err)
	}

	colSystem := -1
	colDistArrival := -1
	colDistRemaining := -1
	colNeutron := -1
	colJumps := -1

	for i, col := range header {
		norm := strings.ToLower(strings.TrimSpace(col))
		switch norm {
		case "system name", "systemname", "system":
			colSystem = i
		case "distance to arrival", "distancetoarrival", "distance_to_arrival":
			colDistArrival = i
		case "distance remaining", "distanceremaining", "distance_remaining":
			colDistRemaining = i
		case "neutron star", "neutronstar", "neutron":
			colNeutron = i
		case "jumps", "jump":
			colJumps = i
		}
	}

	if colSystem == -1 {
		return nil, fmt.Errorf("missing required column 'System Name' in CSV header: %v", header)
	}

	var waypoints []SpanshWaypoint
	lineNum := 1
	for {
		lineNum++
		record, err := reader.Read()
		if err == io.EOF {
			break
		}
		if err != nil {
			return nil, fmt.Errorf("line %d: %w", lineNum, err)
		}

		if len(record) <= colSystem {
			continue
		}

		sysName := strings.TrimSpace(record[colSystem])
		if sysName == "" {
			continue
		}

		wp := SpanshWaypoint{SystemName: sysName}

		if colDistArrival >= 0 && colDistArrival < len(record) {
			if f, err := strconv.ParseFloat(strings.TrimSpace(record[colDistArrival]), 64); err == nil {
				wp.DistanceToArrival = f
			}
		}

		if colDistRemaining >= 0 && colDistRemaining < len(record) {
			if f, err := strconv.ParseFloat(strings.TrimSpace(record[colDistRemaining]), 64); err == nil {
				wp.DistanceRemaining = f
			}
		}

		if colNeutron >= 0 && colNeutron < len(record) {
			val := strings.ToLower(strings.TrimSpace(record[colNeutron]))
			wp.NeutronStar = (val == "yes" || val == "true" || val == "1" || val == "y")
		}

		if colJumps >= 0 && colJumps < len(record) {
			if j, err := strconv.Atoi(strings.TrimSpace(record[colJumps])); err == nil {
				wp.Jumps = j
			}
		}

		waypoints = append(waypoints, wp)
	}

	return waypoints, nil
}

// LoadSpanshRoute reads and parses a route.csv file from disk.
func LoadSpanshRoute(filePath string) ([]SpanshWaypoint, error) {
	f, err := os.Open(filePath)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	return ParseSpanshCSV(f)
}
