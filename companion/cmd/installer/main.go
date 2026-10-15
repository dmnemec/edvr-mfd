package main

import (
	"bufio"
	"bytes"
	"encoding/json"
	"flag"
	"fmt"
	"io"
	"os"
	"path/filepath"
	"regexp"
	"strconv"
	"strings"
)

// findSteamLibraries scans the registry and libraryfolders.vdf for all steam library roots
func findSteamLibraries() []string {
	var roots []string
	seen := make(map[string]bool)

	addRoot := func(p string) {
		if p == "" {
			return
		}
		p = filepath.Clean(p)
		if !seen[strings.ToLower(p)] {
			seen[strings.ToLower(p)] = true
			roots = append(roots, p)
		}
	}

	// 1. Registry Steam InstallPath
	for _, p := range querySteamInstallDirs() {
		addRoot(p)
	}

	// Also check default locations
	addRoot(`C:\Program Files (x86)\Steam`)
	addRoot(`C:\Steam`)
	addRoot(`D:\Steam`)
	addRoot(`D:\SteamLibrary`)

	// 2. Scan libraryfolders.vdf inside all known steam roots
	vdfPattern := regexp.MustCompile(`(?i)"path"\s+"([^"]+)"`)
	for _, root := range roots {
		vdfPath := filepath.Join(root, "steamapps", "libraryfolders.vdf")
		content, err := os.ReadFile(vdfPath)
		if err != nil {
			continue
		}
		matches := vdfPattern.FindAllStringSubmatch(string(content), -1)
		for _, m := range matches {
			if len(m) >= 2 {
				// unescape double slashes
				raw := strings.ReplaceAll(m[1], `\\`, `\`)
				addRoot(raw)
			}
		}
	}

	return roots
}

// findEpicInstallations scans Epic Games manifests
func findEpicInstallations() []string {
	var candidates []string
	manifestDir := `C:\ProgramData\Epic\EpicGamesLauncher\Data\Manifests`
	entries, err := os.ReadDir(manifestDir)
	if err != nil {
		return nil
	}

	for _, e := range entries {
		if strings.HasSuffix(strings.ToLower(e.Name()), ".item") {
			content, err := os.ReadFile(filepath.Join(manifestDir, e.Name()))
			if err != nil {
				continue
			}
			var manifest struct {
				DisplayName    string `json:"DisplayName"`
				InstallLocation string `json:"InstallLocation"`
			}
			if err := json.Unmarshal(content, &manifest); err == nil {
				if strings.Contains(strings.ToLower(manifest.DisplayName), "elite") {
					candidates = append(candidates, manifest.InstallLocation)
				}
			}
		}
	}
	return candidates
}

// findFrontierInstallations scans Frontier launcher registry keys and default directories
func findFrontierInstallations() []string {
	var candidates []string
	candidates = append(candidates, queryFrontierInstallDirs()...)

	candidates = append(candidates,
		`C:\Program Files (x86)\Frontier\EDLaunch\Products`,
		`C:\Program Files\Frontier\EDLaunch\Products`,
		`C:\Users\`+os.Getenv("USERNAME")+`\AppData\Local\Frontier_Developments\Products`,
	)
	return candidates
}

// findOdysseyInstallations searches the entire system for Elite Dangerous Odyssey installations
func findOdysseyInstallations() []string {
	var discovered []string
	seen := make(map[string]bool)

	addCandidate := func(dir string) {
		if dir == "" {
			return
		}
		// Candidate might be either the Products/elite-dangerous-odyssey-64 folder,
		// or the root Elite Dangerous folder. Check both.
		odysseyTarget := dir
		if filepath.Base(strings.ToLower(dir)) != "elite-dangerous-odyssey-64" {
			testSub := filepath.Join(dir, "Products", "elite-dangerous-odyssey-64")
			if fi, err := os.Stat(testSub); err == nil && fi.IsDir() {
				odysseyTarget = testSub
			}
		}

		// Verify that EliteDangerous64.exe or EliteDangerous.exe or d3d11.dll exists
		exe1 := filepath.Join(odysseyTarget, "EliteDangerous64.exe")
		if _, err := os.Stat(exe1); err == nil {
			clean := filepath.Clean(odysseyTarget)
			if !seen[strings.ToLower(clean)] {
				seen[strings.ToLower(clean)] = true
				discovered = append(discovered, clean)
			}
			return
		}

		// Also accept if d3d11.dll or GraphicsConfiguration.xml is present
		cfg := filepath.Join(odysseyTarget, "GraphicsConfiguration.xml")
		if _, err := os.Stat(cfg); err == nil {
			clean := filepath.Clean(odysseyTarget)
			if !seen[strings.ToLower(clean)] {
				seen[strings.ToLower(clean)] = true
				discovered = append(discovered, clean)
			}
		}
	}

	// 1. Current working directory and executable directory
	if cwd, err := os.Getwd(); err == nil {
		addCandidate(cwd)
	}
	if exePath, err := os.Executable(); err == nil {
		addCandidate(filepath.Dir(exePath))
		addCandidate(filepath.Dir(filepath.Dir(exePath)))
	}

	// 2. Steam libraries
	for _, lib := range findSteamLibraries() {
		addCandidate(filepath.Join(lib, "steamapps", "common", "Elite Dangerous", "Products", "elite-dangerous-odyssey-64"))
		addCandidate(filepath.Join(lib, "steamapps", "common", "Elite Dangerous"))
	}

	// 3. Epic installations
	for _, epic := range findEpicInstallations() {
		addCandidate(epic)
		addCandidate(filepath.Join(epic, "Products", "elite-dangerous-odyssey-64"))
	}

	// 4. Frontier installations
	for _, f := range findFrontierInstallations() {
		addCandidate(f)
		addCandidate(filepath.Join(f, "elite-dangerous-odyssey-64"))
	}

	// 5. Common drive letters
	for _, drive := range []string{"C", "D", "E", "F", "G"} {
		addCandidate(drive + `:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64`)
		addCandidate(drive + `:\Games\Elite Dangerous\Products\elite-dangerous-odyssey-64`)
		addCandidate(drive + `:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64`)
		addCandidate(drive + `:\Program Files\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64`)
	}

	return discovered
}

type Version [3]int

func parseVersion(v string) Version {
	clean := strings.TrimPrefix(v, "v")
	parts := strings.Split(clean, "-")[0]
	parts = strings.Split(parts, "+")[0]
	nums := regexp.MustCompile(`\d+`).FindAllString(parts, -1)
	var res Version
	for i := 0; i < len(nums) && i < 3; i++ {
		res[i], _ = strconv.Atoi(nums[i])
	}
	return res
}

func (v Version) GreaterOrEqual(o Version) bool {
	if v[0] != o[0] {
		return v[0] > o[0]
	}
	if v[1] != o[1] {
		return v[1] > o[1]
	}
	return v[2] >= o[2]
}

func readManifestVersion(path string) (string, Version) {
	data, err := os.ReadFile(path)
	if err != nil {
		return "", Version{}
	}
	var m struct {
		Version string `json:"version"`
	}
	if err := json.Unmarshal(data, &m); err != nil {
		return "", Version{}
	}
	return m.Version, parseVersion(m.Version)
}

func copyFile(src, dst string, dryRun bool) error {
	if dryRun {
		fmt.Printf("       copy     %s -> %s (dry run)\n", src, dst)
		return nil
	}
	if err := os.MkdirAll(filepath.Dir(dst), 0755); err != nil {
		return err
	}
	sf, err := os.Open(src)
	if err != nil {
		return err
	}
	defer sf.Close()

	df, err := os.OpenFile(dst, os.O_CREATE|os.O_WRONLY|os.O_TRUNC, 0755)
	if err != nil {
		return err
	}
	defer df.Close()

	_, err = io.Copy(df, sf)
	return err
}

func copyTree(srcDir, dstDir string, dryRun bool) error {
	return filepath.Walk(srcDir, func(path string, info os.FileInfo, err error) error {
		if err != nil {
			return err
		}
		rel, err := filepath.Rel(srcDir, path)
		if err != nil {
			return err
		}
		dstPath := filepath.Join(dstDir, rel)
		if info.IsDir() {
			if dryRun {
				return nil
			}
			return os.MkdirAll(dstPath, 0755)
		}
		return copyFile(path, dstPath, dryRun)
	})
}

func findPackageSourceDir(execDir string) (string, string) {
	// 1. Check if running inside extracted release zip:
	//    execDir/plugins/edvr_mfd/plugin.dll and execDir/plugins/edvr_mfd/mfds/
	c1Dll := filepath.Join(execDir, "plugins", "edvr_mfd", "plugin.dll")
	c1Mfds := filepath.Join(execDir, "plugins", "edvr_mfd", "mfds")
	if fi, err := os.Stat(c1Dll); err == nil && !fi.IsDir() {
		if fi2, err := os.Stat(c1Mfds); err == nil && fi2.IsDir() {
			return c1Dll, c1Mfds
		}
	}

	// 2. Check repo layout:
	//    execDir/../../build/plugins/edvr_mfd/plugin.dll and execDir/../../packages/
	//    or execDir/build/plugins/edvr_mfd/plugin.dll and execDir/packages/
	for _, base := range []string{execDir, filepath.Dir(execDir), filepath.Dir(filepath.Dir(execDir))} {
		c2Dll := filepath.Join(base, "build", "plugins", "edvr_mfd", "plugin.dll")
		c2Mfds := filepath.Join(base, "packages")
		if fi, err := os.Stat(c2Dll); err == nil && !fi.IsDir() {
			if fi2, err := os.Stat(c2Mfds); err == nil && fi2.IsDir() {
				return c2Dll, c2Mfds
			}
		}
	}

	// 3. Check local relative ./plugins/edvr_mfd
	if fi, err := os.Stat("plugins/edvr_mfd/plugin.dll"); err == nil && !fi.IsDir() {
		if fi2, err := os.Stat("plugins/edvr_mfd/mfds"); err == nil && fi2.IsDir() {
			p1, _ := filepath.Abs("plugins/edvr_mfd/plugin.dll")
			p2, _ := filepath.Abs("plugins/edvr_mfd/mfds")
			return p1, p2
		}
	}

	return "", ""
}

func runInstall(gameDir string, srcDll string, srcMfdsDir string, force bool, dryRun bool, verifyOnly bool) (bool, error) {
	dstMfdDir := filepath.Join(gameDir, "plugins", "edvr_mfd")
	dstDll := filepath.Join(dstMfdDir, "plugin.dll")
	dstMfdsDir := filepath.Join(dstMfdDir, "mfds")

	// Check EDVR host
	edvrHostDll := filepath.Join(gameDir, "d3d11.dll")
	if _, err := os.Stat(edvrHostDll); err != nil {
		fmt.Printf("\n[WARNING] EDVR host DLL (d3d11.dll) was NOT found in:\n  %s\n", gameDir)
		fmt.Println("  The MFD mod requires EDVR (edvr-unofficial-patch) to be installed first.")
		fmt.Println("  Download it here: https://github.com/characterecho-sean/edvr-unofficial-patch/releases")
		fmt.Print("  Proceeding with MFD installation anyway...\n\n")
	} else {
		fmt.Printf("[OK] EDVR host (d3d11.dll) verified in game folder.\n")
	}

	// Verify only
	if verifyOnly {
		fmt.Printf("[edvr-mfd] Verifying installation in: %s\n", gameDir)
		srcBytes, err := os.ReadFile(srcDll)
		if err != nil {
			return false, fmt.Errorf("source plugin.dll not found: %w", err)
		}
		dstBytes, err := os.ReadFile(dstDll)
		if err != nil {
			return false, fmt.Errorf("plugin.dll missing in game directory (%s)", dstDll)
		}
		if !bytes.Equal(srcBytes, dstBytes) {
			return false, fmt.Errorf("plugin.dll in game directory does not match source build")
		}

		// Check bundled packages
		entries, _ := os.ReadDir(srcMfdsDir)
		for _, e := range entries {
			if !e.IsDir() {
				continue
			}
			srcManifest := filepath.Join(srcMfdsDir, e.Name(), "manifest.json")
			if _, err := os.Stat(srcManifest); err == nil {
				dstManifest := filepath.Join(dstMfdsDir, e.Name(), "manifest.json")
				if _, err := os.Stat(dstManifest); err != nil {
					return false, fmt.Errorf("package '%s' manifest missing in game directory", e.Name())
				}
			}
		}
		fmt.Println("[SUCCESS] All MFD components verified successfully.")
		return true, nil
	}

	// Perform install
	fmt.Printf("[edvr-mfd] Installing plugin to: %s\n", dstDll)
	if err := copyFile(srcDll, dstDll, dryRun); err != nil {
		return false, fmt.Errorf("failed to install plugin.dll: %w", err)
	}

	// Remove legacy flat dll if present
	legacyDll := filepath.Join(gameDir, "plugins", "edvr_mfd.dll")
	if _, err := os.Stat(legacyDll); err == nil {
		if !dryRun {
			_ = os.Remove(legacyDll)
			fmt.Println("[edvr-mfd] Removed legacy edvr_mfd.dll")
		}
	}

	// Install packages with version-aware protection
	entries, err := os.ReadDir(srcMfdsDir)
	if err != nil {
		return false, fmt.Errorf("failed to read packages source: %w", err)
	}

	for _, e := range entries {
		if !e.IsDir() {
			continue
		}
		pkgID := e.Name()
		srcPkgDir := filepath.Join(srcMfdsDir, pkgID)
		dstPkgDir := filepath.Join(dstMfdsDir, pkgID)

		srcManifestPath := filepath.Join(srcPkgDir, "manifest.json")
		if _, err := os.Stat(srcManifestPath); err != nil {
			continue
		}

		srcVerStr, srcVer := readManifestVersion(srcManifestPath)
		dstManifestPath := filepath.Join(dstPkgDir, "manifest.json")
		dstVerStr, dstVer := readManifestVersion(dstManifestPath)

		if dstVerStr != "" && !force && dstVer.GreaterOrEqual(srcVer) {
			fmt.Printf("       skip     package '%s' (installed version %s >= source %s, use --force to overwrite)\n", pkgID, dstVerStr, srcVerStr)
			continue
		}

		fmt.Printf("       install  package '%s' (v%s) -> %s\n", pkgID, srcVerStr, dstPkgDir)
		if err := copyTree(srcPkgDir, dstPkgDir, dryRun); err != nil {
			return false, fmt.Errorf("failed to install package '%s': %w", pkgID, err)
		}
	}

	fmt.Printf("\n[SUCCESS] EDVR MFD Plugin installed successfully to:\n  %s\n", gameDir)
	return true, nil
}

func main() {
	targetFlag := flag.String("target", "", "Path to Elite Dangerous Odyssey installation directory")
	forceFlag := flag.Bool("force", false, "Force overwrite existing packages regardless of version")
	dryRunFlag := flag.Bool("dry-run", false, "Preview installation actions without modifying files")
	verifyFlag := flag.Bool("verify-only", false, "Verify that installed files match source")
	batchFlag := flag.Bool("batch", false, "Run in non-interactive batch mode (no pause at exit)")
	flag.Parse()

	reader := bufio.NewReader(os.Stdin)
	pauseAtExit := !*batchFlag

	defer func() {
		if pauseAtExit {
			fmt.Print("\nPress Enter to exit...")
			_, _ = reader.ReadString('\n')
		}
	}()

	fmt.Println("==================================================================")
	fmt.Println("               EDVR MFD Plugin - Native Installer                 ")
	fmt.Println("==================================================================")

	exePath, err := os.Executable()
	if err != nil {
		fmt.Printf("[ERROR] Could not resolve installer executable path: %v\n", err)
		os.Exit(1)
	}
	execDir := filepath.Dir(exePath)

	srcDll, srcMfdsDir := findPackageSourceDir(execDir)
	if srcDll == "" || srcMfdsDir == "" {
		fmt.Println("[ERROR] Could not locate MFD release files (plugin.dll and mfds/).")
		fmt.Println("Please make sure install_mfd.exe is located inside the unzipped release folder.")
		os.Exit(1)
	}

	selectedDir := *targetFlag
	if selectedDir == "" {
		fmt.Println("\nSearching for Elite Dangerous Odyssey installation...")
		found := findOdysseyInstallations()

		if len(found) == 0 {
			fmt.Println("[!] Could not auto-detect your Elite Dangerous Odyssey folder.")
			fmt.Print("\nPlease enter the path to your 'elite-dangerous-odyssey-64' folder: ")
			input, _ := reader.ReadString('\n')
			input = strings.TrimSpace(input)
			input = strings.Trim(input, `"'`)
			if input == "" {
				fmt.Println("[ERROR] No path provided. Installation cancelled.")
				os.Exit(1)
			}
			selectedDir = input
		} else if len(found) == 1 {
			selectedDir = found[0]
			fmt.Printf("Found game directory: %s\n", selectedDir)
		} else {
			fmt.Printf("Found %d game installations:\n", len(found))
			for i, p := range found {
				fmt.Printf("  [%d] %s\n", i+1, p)
			}
			for {
				fmt.Printf("\nSelect installation to install to (1-%d): ", len(found))
				input, _ := reader.ReadString('\n')
				input = strings.TrimSpace(input)
				idx, err := strconv.Atoi(input)
				if err == nil && idx >= 1 && idx <= len(found) {
					selectedDir = found[idx-1]
					break
				}
				fmt.Println("Invalid selection. Please enter a number from the list.")
			}
		}
	}

	// Validate selected directory
	fi, err := os.Stat(selectedDir)
	if err != nil || !fi.IsDir() {
		fmt.Printf("[ERROR] The specified path is not a valid directory:\n  %s\n", selectedDir)
		os.Exit(1)
	}

	ok, err := runInstall(selectedDir, srcDll, srcMfdsDir, *forceFlag, *dryRunFlag, *verifyFlag)
	if err != nil {
		fmt.Printf("\n[ERROR] Installation failed: %v\n", err)
		os.Exit(1)
	}
	if !ok {
		os.Exit(1)
	}
}
