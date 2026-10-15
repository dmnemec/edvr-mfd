package main

import (
	"context"
	"flag"
	"fmt"
	"io"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/manifest"
	"github.com/dmnemec/edvr-mfd/companion/pkg/mfdkit/replay"
)

func printUsage(w io.Writer) {
	fmt.Fprintf(w, `mfdkit - Elite Dangerous VR MFD development and testing toolkit

Usage:
  mfdkit <command> [arguments]

Available commands:
  validate <package_dir>
      Validate manifest.json, screen display models, and companion declarations.
  record --out <session_dir> [--journal-dir <dir>] [--commander <name>] [--notes <notes>] [--duration <dur>]
      Record a live game session with timestamped delta logs.
  replay --in <session_dir> --out <dest_dir> [--speed 1x|2x|4x|instant]
      Replay a recorded session into a destination directory at specified speed.

Run 'mfdkit <command> -h' for more details on a specific command.
`)
}

func run(args []string, stdout, stderr io.Writer) int {
	if len(args) == 0 {
		printUsage(stderr)
		return 1
	}

	cmd := args[0]
	cmdArgs := args[1:]

	switch cmd {
	case "validate":
		return runValidate(cmdArgs, stdout, stderr)
	case "record":
		return runRecord(cmdArgs, stdout, stderr)
	case "replay":
		return runReplay(cmdArgs, stdout, stderr)
	case "-h", "--help", "help":
		printUsage(stdout)
		return 0
	default:
		fmt.Fprintf(stderr, "unknown command: %s\n\n", cmd)
		printUsage(stderr)
		return 1
	}
}

func runValidate(args []string, stdout, stderr io.Writer) int {
	fs := flag.NewFlagSet("validate", flag.ContinueOnError)
	fs.SetOutput(stderr)
	fs.Usage = func() {
		fmt.Fprintf(stderr, "Usage: mfdkit validate <package_dir>\n\nValidates package manifest.json, all referenced screens, and companion declarations.\n")
	}

	if err := fs.Parse(args); err != nil {
		return 1
	}

	rest := fs.Args()
	if len(rest) < 1 {
		fmt.Fprintf(stderr, "error: package directory argument is required\n\n")
		fs.Usage()
		return 1
	}

	pkgDir := rest[0]
	m, errs := manifest.ValidatePackage(pkgDir)
	if len(errs) > 0 {
		fmt.Fprintf(stderr, "Package validation FAILED for %q with %d error(s):\n", pkgDir, len(errs))
		for i, e := range errs {
			fmt.Fprintf(stderr, "  [%d] %s\n", i+1, e)
		}
		return 1
	}

	screenCount := 0
	compCount := 0
	if m != nil {
		screenCount = len(m.Screens)
		compCount = len(m.Companions)
	}

	fmt.Fprintf(stdout, "Package %q validated successfully (%d screen(s), %d companion(s))\n", m.ID, screenCount, compCount)
	return 0
}

func runRecord(args []string, stdout, stderr io.Writer) int {
	fs := flag.NewFlagSet("record", flag.ContinueOnError)
	fs.SetOutput(stderr)

	var outDir string
	var journalDir string
	var commander string
	var notes string
	var durationStr string

	fs.StringVar(&outDir, "out", "", "Directory to save recorded session files (required)")
	fs.StringVar(&journalDir, "journal-dir", "", "Elite Dangerous journal directory (default: auto-discover or EDVR_MFD_JOURNAL_DIR)")
	fs.StringVar(&commander, "commander", "", "Commander name override")
	fs.StringVar(&notes, "notes", "", "Notes or description for this session")
	fs.StringVar(&durationStr, "duration", "", "Optional maximum recording duration (e.g. 30s, 5m)")

	if err := fs.Parse(args); err != nil {
		return 1
	}

	if outDir == "" {
		fmt.Fprintf(stderr, "error: --out <session_dir> is required\n")
		return 1
	}

	var duration time.Duration
	if durationStr != "" {
		d, err := time.ParseDuration(durationStr)
		if err != nil {
			fmt.Fprintf(stderr, "error: invalid duration %q: %v\n", durationStr, err)
			return 1
		}
		duration = d
	}

	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()

	rec := replay.NewRecorder(replay.RecordConfig{
		JournalDir: journalDir,
		OutputDir:  outDir,
		Commander:  commander,
		Notes:      notes,
		Duration:   duration,
	})

	fmt.Fprintf(stdout, "Recording session to %q... (Press Ctrl+C to stop)\n", outDir)
	meta, err := rec.Record(ctx)
	if err != nil {
		fmt.Fprintf(stderr, "recording failed: %v\n", err)
		return 1
	}

	fmt.Fprintf(stdout, "Recording stopped. Session %q saved with %d event(s).\n", meta.SessionID, meta.EventsCount)
	return 0
}

func runReplay(args []string, stdout, stderr io.Writer) int {
	fs := flag.NewFlagSet("replay", flag.ContinueOnError)
	fs.SetOutput(stderr)

	var inDir string
	var outDir string
	var speedStr string

	fs.StringVar(&inDir, "in", "", "Session directory containing recorded files (required)")
	fs.StringVar(&outDir, "out", "", "Target destination directory mimicking journal directory (required)")
	fs.StringVar(&speedStr, "speed", "1x", "Playback speed multiplier: 1x, 2x, 4x, instant")

	if err := fs.Parse(args); err != nil {
		return 1
	}

	if inDir == "" {
		fmt.Fprintf(stderr, "error: --in <session_dir> is required\n")
		return 1
	}
	if outDir == "" {
		fmt.Fprintf(stderr, "error: --out <dest_dir> is required\n")
		return 1
	}

	speed, err := replay.ParseSpeed(speedStr)
	if err != nil {
		fmt.Fprintf(stderr, "error: %v\n", err)
		return 1
	}

	ctx, cancel := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer cancel()

	rp := replay.NewReplayer(replay.ReplayConfig{
		SessionDir: inDir,
		DestDir:    outDir,
		Speed:      speed,
	})

	speedLabel := speedStr
	if speed == 0 {
		speedLabel = "instant"
	}
	fmt.Fprintf(stdout, "Replaying session from %q to %q (speed: %s)...\n", inDir, outDir, speedLabel)

	if err := rp.Replay(ctx); err != nil {
		if err == context.Canceled {
			fmt.Fprintf(stdout, "Replay canceled.\n")
			return 0
		}
		fmt.Fprintf(stderr, "replay failed: %v\n", err)
		return 1
	}

	fmt.Fprintf(stdout, "Replay completed successfully.\n")
	return 0
}

func main() {
	os.Exit(run(os.Args[1:], os.Stdout, os.Stderr))
}
