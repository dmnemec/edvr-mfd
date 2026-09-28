#!/usr/bin/env python3
"""
Install and manage the standalone Cockpit MFD plugin for EDVR.
Installs build/plugins/edvr_mfd/ to <game_dir>/plugins/edvr_mfd/
"""

import argparse
import os
import shutil
import sys


def find_game_dir(target_arg=None):
    if target_arg:
        if os.path.isdir(target_arg):
            return os.path.abspath(target_arg)
        raise FileNotFoundError(f"Specified target directory not found: {target_arg}")

    default_paths = [
        r"D:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64",
        r"C:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64",
        r"C:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64",
    ]
    for p in default_paths:
        if os.path.isdir(p):
            return os.path.abspath(p)
    return None


def install_mfd(repo_root, game_dir, dry_run=False, verify_only=False):
    src_plugin_dir = os.path.join(repo_root, "build", "plugins", "edvr_mfd")
    src_dll = os.path.join(src_plugin_dir, "plugin.dll")

    if not os.path.isfile(src_dll):
        raise FileNotFoundError(f"Built plugin DLL not found: {src_dll}. Run build.bat first.")

    dst_plugins_dir = os.path.join(game_dir, "plugins")
    dst_mfd_dir = os.path.join(dst_plugins_dir, "edvr_mfd")
    dst_dll = os.path.join(dst_mfd_dir, "plugin.dll")

    # Verification mode
    if verify_only:
        if not os.path.isfile(dst_dll):
            print(f"[edvr-mfd] NOT INSTALLED: {dst_dll} missing")
            return 1
        with open(src_dll, "rb") as f1, open(dst_dll, "rb") as f2:
            if f1.read() == f2.read():
                print(f"[edvr-mfd] VERIFIED: {dst_dll} matches build")
                return 0
            else:
                print(f"[edvr-mfd] MISMATCH: {dst_dll} does not match build")
                return 2

    # Clean legacy flat files if present
    legacy_flat_dll = os.path.join(dst_plugins_dir, "edvr_mfd.dll")
    if os.path.isfile(legacy_flat_dll):
        if dry_run:
            print(f"       remove   legacy {legacy_flat_dll} (dry run)")
        else:
            try:
                os.remove(legacy_flat_dll)
                print(f"[edvr-mfd] removed legacy {legacy_flat_dll}")
            except Exception as e:
                print(f"[edvr-mfd] warning: could not remove legacy {legacy_flat_dll}: {e}")

    if dry_run:
        print(f"       mkdir    {dst_mfd_dir} (dry run)")
        print(f"       install  {src_dll} -> {dst_dll} (dry run)")
        return 0

    os.makedirs(dst_mfd_dir, exist_ok=True)
    shutil.copy2(src_dll, dst_dll)
    print(f"[edvr-mfd] INSTALLED: {dst_dll}")

    # Copy display templates if available
    src_displays = os.path.join(src_plugin_dir, "displays")
    dst_displays = os.path.join(dst_mfd_dir, "displays")
    if os.path.isdir(src_displays):
        os.makedirs(dst_displays, exist_ok=True)
        for item in os.listdir(src_displays):
            s = os.path.join(src_displays, item)
            d = os.path.join(dst_displays, item)
            if os.path.isfile(s) and not os.path.exists(d):
                shutil.copy2(s, d)
                print(f"[edvr-mfd] staged display template: {d}")

    return 0


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        repo_root = os.path.join(tmp, "repo")
        game_dir = os.path.join(tmp, "game")
        build_dir = os.path.join(repo_root, "build", "plugins", "edvr_mfd")
        os.makedirs(build_dir, exist_ok=True)
        os.makedirs(game_dir, exist_ok=True)

        fake_dll = os.path.join(build_dir, "plugin.dll")
        with open(fake_dll, "wb") as f:
            f.write(b"EDVR_MFD_PLUGIN_TEST_BYTES")

        # Dry run test
        res = install_mfd(repo_root, game_dir, dry_run=True)
        assert res == 0
        assert not os.path.exists(os.path.join(game_dir, "plugins", "edvr_mfd", "plugin.dll"))

        # Actual install test
        res = install_mfd(repo_root, game_dir, dry_run=False)
        assert res == 0
        installed_dll = os.path.join(game_dir, "plugins", "edvr_mfd", "plugin.dll")
        assert os.path.isfile(installed_dll)
        with open(installed_dll, "rb") as f:
            assert f.read() == b"EDVR_MFD_PLUGIN_TEST_BYTES"

        # Verify test
        res = install_mfd(repo_root, game_dir, verify_only=True)
        assert res == 0

    print("[edvr-mfd] self-test: PASSED")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Install Cockpit MFD Plugin for EDVR")
    parser.add_argument("--target", help="Path to Elite Dangerous product folder")
    parser.add_argument("--dry-run", action="store_true", help="Print actions without modifying disk")
    parser.add_argument("--verify-only", action="store_true", help="Verify installed plugin matches build")
    parser.add_argument("--self-test", action="store_true", help="Run internal self-tests and exit")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    game_dir = find_game_dir(args.target)
    if not game_dir:
        print("[edvr-mfd] ERROR: Game directory not found. Please pass --target <path>", file=sys.stderr)
        return 1

    return install_mfd(repo_root, game_dir, dry_run=args.dry_run, verify_only=args.verify_only)


if __name__ == "__main__":
    sys.exit(main())
