package main

import (
	"encoding/json"
	"os"
	"path/filepath"
	"strings"
	"sync"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/elite"
)

// PriceCache manages the persistent commodity MeanPrice cache per §12.1.
// Stored in %LOCALAPPDATA%\EDVR\mfd-companion\mean_prices.json.
type PriceCache struct {
	mu       sync.RWMutex
	filePath string
	prices   map[string]int64
}

// NewPriceCache initializes a PriceCache pointing to filePath.
func NewPriceCache(filePath string) *PriceCache {
	pc := &PriceCache{
		filePath: filePath,
		prices:   make(map[string]int64),
	}
	_ = pc.Load()
	return pc
}

// Load reads the price cache from disk if present.
func (pc *PriceCache) Load() error {
	pc.mu.Lock()
	defer pc.mu.Unlock()

	data, err := os.ReadFile(pc.filePath)
	if err != nil {
		return err
	}

	var loaded map[string]int64
	if err := json.Unmarshal(data, &loaded); err != nil {
		return err
	}

	for k, v := range loaded {
		pc.prices[strings.ToLower(k)] = v
		clean := elite.CleanItemName(k)
		if clean != "" {
			pc.prices[clean] = v
		}
	}
	return nil
}

// Save atomically persists the cache to disk via temporary file rename.
func (pc *PriceCache) Save() error {
	pc.mu.RLock()
	data, err := json.MarshalIndent(pc.prices, "", "  ")
	pc.mu.RUnlock()

	if err != nil {
		return err
	}
	data = append(data, '\n')

	dir := filepath.Dir(pc.filePath)
	if err := os.MkdirAll(dir, 0755); err != nil {
		return err
	}

	tmpFile := pc.filePath + ".tmp"
	if err := os.WriteFile(tmpFile, data, 0644); err != nil {
		return err
	}

	return os.Rename(tmpFile, pc.filePath)
}

// Get returns the galactic average MeanPrice for the given commodity name.
func (pc *PriceCache) Get(name string) (int64, bool) {
	pc.mu.RLock()
	defer pc.mu.RUnlock()

	lower := strings.ToLower(name)
	if p, ok := pc.prices[lower]; ok && p > 0 {
		return p, true
	}

	clean := elite.CleanItemName(name)
	if p, ok := pc.prices[clean]; ok && p > 0 {
		return p, true
	}

	return 0, false
}

// UpdateFromMarket incorporates MeanPrice from Market.json items and saves to disk.
func (pc *PriceCache) UpdateFromMarket(market *elite.MarketSnapshot) bool {
	if market == nil || len(market.Items) == 0 {
		return false
	}

	pc.mu.Lock()
	changed := false
	for _, item := range market.Items {
		if item.MeanPrice <= 0 {
			continue
		}

		keys := []string{
			strings.ToLower(item.Name),
			strings.ToLower(item.NameLocalised),
			elite.CleanItemName(item.Name),
		}

		for _, k := range keys {
			if k == "" {
				continue
			}
			if old, ok := pc.prices[k]; !ok || old != item.MeanPrice {
				pc.prices[k] = item.MeanPrice
				changed = true
			}
		}
	}
	pc.mu.Unlock()

	if changed {
		_ = pc.Save()
	}
	return changed
}
