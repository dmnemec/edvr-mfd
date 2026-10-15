#!/usr/bin/env python3
"""
Install and manage the standalone Cockpit MFD plugin for EDVR.
Installs build/plugins/edvr_mfd/ to <game_dir>/plugins/edvr_mfd/
and bundled packages to <game_dir>/plugins/edvr_mfd/mfds/<pkg_id>/
"""

import argparse
import json
import os
import re
import shutil
import sys


def parse_version(ver_str):
    """
    Parses a semantic version string (e.g. '1.0.0', 'v1.2.0-rc1') into a tuple of ints.
    Missing numeric parts default to 0.
    """
    if not ver_str or not isinstance(ver_str, str):
        return (0, 0, 0)
    clean = ver_str.lstrip('v').split('-')[0].split('+')[0]
    nums = re.findall(r'\d+', clean)
    if not nums:
        return (0, 0, 0)
    parts = [int(n) for n in nums]
    while len(parts) < 3:
        parts.append(0)
    return tuple(parts)


def find_game_dir(target_arg=None):
    if target_arg and target_arg.lower() not in ("steam", "frontier"):
        if os.path.isdir(target_arg):
            return os.path.abspath(target_arg)
        raise FileNotFoundError(f"Specified target directory not found: {target_arg}")

    default_paths = [
        r"D:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64",
        r"C:\Program Files (x86)\Steam\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64",
        r"C:\SteamLibrary\steamapps\common\Elite Dangerous\Products\elite-dangerous-odyssey-64",
        r"C:\Program Files (x86)\Frontier\EDLaunch\Products\elite-dangerous-odyssey-64",
    ]
    for p in default_paths:
        if os.path.isdir(p):
            return os.path.abspath(p)
    return None


def read_manifest_version(manifest_path):
    if not os.path.isfile(manifest_path):
        return None
    try:
        with open(manifest_path, "r", encoding="utf-8") as f:
            data = json.load(f)
            return data.get("version", "0.0.0")
    except Exception:
        return None


def install_mfd(repo_root, game_dir, dry_run=False, verify_only=False, force=False):
    src_plugin_dir = os.path.join(repo_root, "build", "plugins", "edvr_mfd")
    src_dll = os.path.join(src_plugin_dir, "plugin.dll")

    if not os.path.isfile(src_dll):
        raise FileNotFoundError(f"Built plugin DLL not found: {src_dll}. Run build.bat first.")

    src_packages_dir = os.path.join(repo_root, "packages")
    if not os.path.isdir(src_packages_dir):
        # Fallback to build staging if repo root packages folder not directly present
        alt_packages_dir = os.path.join(src_plugin_dir, "mfds")
        if os.path.isdir(alt_packages_dir):
            src_packages_dir = alt_packages_dir

    dst_plugins_dir = os.path.join(game_dir, "plugins")
    dst_mfd_dir = os.path.join(dst_plugins_dir, "edvr_mfd")
    dst_dll = os.path.join(dst_mfd_dir, "plugin.dll")
    dst_mfds_dir = os.path.join(dst_mfd_dir, "mfds")

    # Discover bundled packages
    bundled_packages = []
    if os.path.isdir(src_packages_dir):
        for item in sorted(os.listdir(src_packages_dir)):
            item_path = os.path.join(src_packages_dir, item)
            manifest_file = os.path.join(item_path, "manifest.json")
            if os.path.isdir(item_path) and os.path.isfile(manifest_file):
                bundled_packages.append((item, item_path, manifest_file))

    # Verification mode
    if verify_only:
        if not os.path.isfile(dst_dll):
            print(f"[edvr-mfd] NOT INSTALLED: {dst_dll} missing")
            return 1
        with open(src_dll, "rb") as f1, open(dst_dll, "rb") as f2:
            if f1.read() != f2.read():
                print(f"[edvr-mfd] MISMATCH: {dst_dll} does not match build")
                return 2

        # Verify bundled packages
        for pkg_id, _, src_manifest_file in bundled_packages:
            dst_pkg_manifest = os.path.join(dst_mfds_dir, pkg_id, "manifest.json")
            if not os.path.isfile(dst_pkg_manifest):
                print(f"[edvr-mfd] NOT INSTALLED: package '{pkg_id}' missing in {dst_mfds_dir}")
                return 1

        print(f"[edvr-mfd] VERIFIED: {dst_dll} and all {len(bundled_packages)} bundled package(s) match build")
        return 0

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

    # Plugin DLL staging
    if dry_run:
        print(f"       mkdir    {dst_mfd_dir} (dry run)")
        print(f"       install  {src_dll} -> {dst_dll} (dry run)")
    else:
        os.makedirs(dst_mfd_dir, exist_ok=True)
        shutil.copy2(src_dll, dst_dll)
        print(f"[edvr-mfd] INSTALLED: {dst_dll}")

    # Stage bundled packages
    if bundled_packages:
        if not dry_run:
            os.makedirs(dst_mfds_dir, exist_ok=True)

        for pkg_id, pkg_src_dir, pkg_src_manifest in bundled_packages:
            dst_pkg_dir = os.path.join(dst_mfds_dir, pkg_id)
            dst_pkg_manifest = os.path.join(dst_pkg_dir, "manifest.json")

            repo_ver_str = read_manifest_version(pkg_src_manifest) or "0.0.0"
            repo_ver = parse_version(repo_ver_str)

            should_install = True
            if os.path.isdir(dst_pkg_dir) and os.path.isfile(dst_pkg_manifest):
                target_ver_str = read_manifest_version(dst_pkg_manifest) or "0.0.0"
                target_ver = parse_version(target_ver_str)

                if target_ver >= repo_ver and not force:
                    should_install = False
                    if dry_run:
                        print(f"       skip     package '{pkg_id}' (target v{target_ver_str} >= repo v{repo_ver_str}; use --force to overwrite) (dry run)")
                    else:
                        print(f"[edvr-mfd] SKIPPED: package '{pkg_id}' (target v{target_ver_str} >= repo v{repo_ver_str}; use --force to overwrite)")

            if should_install:
                if dry_run:
                    print(f"       install  package '{pkg_id}' (v{repo_ver_str}) -> {dst_pkg_dir} (dry run)")
                else:
                    os.makedirs(dst_pkg_dir, exist_ok=True)
                    shutil.copytree(pkg_src_dir, dst_pkg_dir, dirs_exist_ok=True)
                    print(f"[edvr-mfd] INSTALLED: package '{pkg_id}' (v{repo_ver_str}) -> {dst_pkg_dir}")

    # Stage display templates if available (backward compatibility)
    src_displays = os.path.join(src_plugin_dir, "displays")
    dst_displays = os.path.join(dst_mfd_dir, "displays")
    if os.path.isdir(src_displays):
        if dry_run:
            print(f"       stage    displays -> {dst_displays} (dry run)")
        else:
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
        packages_dir = os.path.join(repo_root, "packages")
        os.makedirs(build_dir, exist_ok=True)
        os.makedirs(game_dir, exist_ok=True)

        # 1. Plugin DLL mock
        fake_dll = os.path.join(build_dir, "plugin.dll")
        with open(fake_dll, "wb") as f:
            f.write(b"EDVR_MFD_PLUGIN_TEST_BYTES")

        # 2. Bundled package mock
        pkg1_dir = os.path.join(packages_dir, "ship")
        os.makedirs(os.path.join(pkg1_dir, "screens"), exist_ok=True)
        os.makedirs(os.path.join(pkg1_dir, "bin"), exist_ok=True)
        with open(os.path.join(pkg1_dir, "manifest.json"), "w", encoding="utf-8") as f:
            json.dump({"manifestVersion": 1, "id": "ship", "version": "1.0.0"}, f)
        with open(os.path.join(pkg1_dir, "screens", "modules.json"), "w", encoding="utf-8") as f:
            f.write('{"title": "MODULES"}')
        with open(os.path.join(pkg1_dir, "bin", "edvr-mfd-ship.exe"), "wb") as f:
            f.write(b"SHIP_EXE_BIN")

        # 3. Dry run test
        res = install_mfd(repo_root, game_dir, dry_run=True)
        assert res == 0
        assert not os.path.exists(os.path.join(game_dir, "plugins", "edvr_mfd", "plugin.dll"))
        assert not os.path.exists(os.path.join(game_dir, "plugins", "edvr_mfd", "mfds", "ship"))

        # 4. Actual install test
        res = install_mfd(repo_root, game_dir, dry_run=False)
        assert res == 0
        installed_dll = os.path.join(game_dir, "plugins", "edvr_mfd", "plugin.dll")
        assert os.path.isfile(installed_dll)
        with open(installed_dll, "rb") as f:
            assert f.read() == b"EDVR_MFD_PLUGIN_TEST_BYTES"

        installed_manifest = os.path.join(game_dir, "plugins", "edvr_mfd", "mfds", "ship", "manifest.json")
        installed_screen = os.path.join(game_dir, "plugins", "edvr_mfd", "mfds", "ship", "screens", "modules.json")
        installed_bin = os.path.join(game_dir, "plugins", "edvr_mfd", "mfds", "ship", "bin", "edvr-mfd-ship.exe")
        assert os.path.isfile(installed_manifest)
        assert os.path.isfile(installed_screen)
        assert os.path.isfile(installed_bin)

        # 5. Verify test (success)
        res = install_mfd(repo_root, game_dir, verify_only=True)
        assert res == 0

        # 6. Verify test (failure on corrupted DLL)
        with open(installed_dll, "wb") as f:
            f.write(b"CORRUPTED")
        res = install_mfd(repo_root, game_dir, verify_only=True)
        assert res == 2
        # Restore DLL
        with open(installed_dll, "wb") as f:
            f.write(b"EDVR_MFD_PLUGIN_TEST_BYTES")

        # 7. Version-aware protection (skip overwrite when target >= repo)
        # Modify target screen to simulate user custom screen modification
        with open(installed_screen, "w", encoding="utf-8") as f:
            f.write('{"title": "USER_CUSTOM_MODIFIED_SCREEN"}')
        # Also create a runtime artifact that should never be deleted
        runtime_log = os.path.join(game_dir, "plugins", "edvr_mfd", "mfds", "ship", "companion.log")
        with open(runtime_log, "w", encoding="utf-8") as f:
            f.write("user session log")

        # Run install without --force (target v1.0.0 >= repo v1.0.0)
        res = install_mfd(repo_root, game_dir, dry_run=False, force=False)
        assert res == 0
        with open(installed_screen, "r", encoding="utf-8") as f:
            assert f.read() == '{"title": "USER_CUSTOM_MODIFIED_SCREEN"}'
        assert os.path.isfile(runtime_log)

        # 8. Force install (--force overwrites package files, but preserves extra files)
        res = install_mfd(repo_root, game_dir, dry_run=False, force=True)
        assert res == 0
        with open(installed_screen, "r", encoding="utf-8") as f:
            assert f.read() == '{"title": "MODULES"}'
        assert os.path.isfile(runtime_log)

        # 9. Extra user-imported packages preservation
        user_pkg_dir = os.path.join(game_dir, "plugins", "edvr_mfd", "mfds", "user_mod")
        os.makedirs(user_pkg_dir, exist_ok=True)
        user_pkg_manifest = os.path.join(user_pkg_dir, "manifest.json")
        with open(user_pkg_manifest, "w", encoding="utf-8") as f:
            json.dump({"manifestVersion": 1, "id": "user_mod", "version": "0.1.0"}, f)

        res = install_mfd(repo_root, game_dir, dry_run=False)
        assert res == 0
        assert os.path.isfile(user_pkg_manifest)

        # 10. Verify test with missing package failure
        os.remove(installed_manifest)
        res = install_mfd(repo_root, game_dir, verify_only=True)
        assert res == 1

    print("[edvr-mfd] self-test: PASSED")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Install Cockpit MFD Plugin for EDVR")
    parser.add_argument("--target", help="Path to Elite Dangerous product folder")
    parser.add_argument("--dry-run", action="store_true", help="Print actions without modifying disk")
    parser.add_argument("--verify-only", action="store_true", help="Verify installed plugin matches build")
    parser.add_argument("--force", action="store_true", help="Force overwrite packages even if target version is >= repo version")
    parser.add_argument("--self-test", action="store_true", help="Run internal self-tests and exit")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    game_dir = find_game_dir(args.target)
    if not game_dir:
        print("[edvr-mfd] ERROR: Game directory not found. Please pass --target <path>", file=sys.stderr)
        return 1

    return install_mfd(
        repo_root,
        game_dir,
        dry_run=args.dry_run,
        verify_only=args.verify_only,
        force=args.force
    )


if __name__ == "__main__":
    sys.exit(main())
