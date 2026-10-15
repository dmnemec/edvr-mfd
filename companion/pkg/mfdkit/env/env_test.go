package env

import (
	"path/filepath"
	"testing"
)

func TestEnvPackageDir(t *testing.T) {
	temp := t.TempDir()
	t.Setenv(EnvPackageDir, temp)
	if PackageDir() != temp {
		t.Fatalf("expected PackageDir %q, got %q", temp, PackageDir())
	}
}

func TestEnvScreenPath(t *testing.T) {
	pkg := filepath.Join("test", "pkg")
	p1 := ScreenPath(pkg, "modules")
	expected1 := filepath.Join(pkg, "screens", "modules.json")
	if p1 != expected1 {
		t.Fatalf("expected %q, got %q", expected1, p1)
	}

	p2 := ScreenPath(pkg, "screens/power.json")
	expected2 := filepath.Join(pkg, "screens", "power.json")
	if p2 != expected2 {
		t.Fatalf("expected %q, got %q", expected2, p2)
	}
}

func TestEnvMeanPricesPath(t *testing.T) {
	custom := filepath.Join(t.TempDir(), "custom_prices.json")
	t.Setenv(EnvPriceCache, custom)
	if MeanPricesPath() != custom {
		t.Fatalf("expected custom path %q, got %q", custom, MeanPricesPath())
	}

	t.Setenv(EnvPriceCache, "")
	t.Setenv("LOCALAPPDATA", filepath.Join("C:", "Users", "Test", "AppData", "Local"))
	p := MeanPricesPath()
	if !filepath.IsAbs(p) && len(p) == 0 {
		t.Fatalf("expected non-empty path, got %q", p)
	}
}
