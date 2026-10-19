#!/usr/bin/env python3
"""
Package release artifact for EDVR Cockpit MFD (Multi-Function Display) plugin.
Validates all packages using `mfdkit validate` and packages `edvr-mfd-v1.0.0.zip`.
"""

import argparse
import os
import shutil
import subprocess
import sys
import zipfile


def to_wsl_path(win_path):
    p = os.path.abspath(win_path).replace("\\", "/")
    if len(p) >= 2 and p[1] == ":":
        return f"/mnt/{p[0].lower()}{p[2:]}"
    return p


def validate_package(repo_root, pkg_dir):
    """
    Validates a package directory using mfdkit validate.
    Attempts native mfdkit binary first, falls back to WSL go run.
    """
    # 1. Native mfdkit
    native_mfdkit = os.path.join(repo_root, "build", "tools", "mfdkit.exe")
    if not os.path.isfile(native_mfdkit):
        which_mfdkit = shutil.which("mfdkit")
        if which_mfdkit:
            native_mfdkit = which_mfdkit

    if os.path.isfile(native_mfdkit):
        cmd = [native_mfdkit, "validate", os.path.abspath(pkg_dir)]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode == 0:
            print(f"[package_release] validated (native): {pkg_dir}")
            return True, res.stdout.strip()
        else:
            return False, res.stderr.strip() or res.stdout.strip()

    companion_dir = os.path.join(repo_root, "companion")

    # 2. Windows Go: EDVR_GO (path to go.exe) or go on PATH, same order as build.bat
    native_go = os.environ.get("EDVR_GO") or shutil.which("go")
    if native_go:
        cmd = [native_go, "run", "./cmd/mfdkit", "validate", os.path.abspath(pkg_dir)]
        res = subprocess.run(cmd, capture_output=True, text=True, cwd=companion_dir)
        if res.returncode == 0:
            print(f"[package_release] validated (go): {pkg_dir}")
            return True, res.stdout.strip()
        else:
            return False, res.stderr.strip() or res.stdout.strip()

    # 3. WSL fallback: Go from the login shell PATH, plus the default /usr/local/go/bin
    if shutil.which("wsl"):
        wsl_pkg = to_wsl_path(pkg_dir)
        cmd = [
            "wsl", "--cd", os.path.abspath(companion_dir), "--exec", "bash", "-lc",
            f"export PATH=$PATH:/usr/local/go/bin; go run ./cmd/mfdkit validate '{wsl_pkg}'"
        ]
        res = subprocess.run(cmd, capture_output=True, text=True)
        if res.returncode == 0:
            print(f"[package_release] validated (wsl): {pkg_dir}")
            return True, res.stdout.strip()
        else:
            return False, res.stderr.strip() or res.stdout.strip()

    return False, "mfdkit not found: build it, put Go on PATH (or set EDVR_GO), or install Go in WSL"


def create_release_zip(repo_root, output_zip_path, skip_validate=False):
    repo_root = os.path.abspath(repo_root)
    plugin_dll = os.path.join(repo_root, "build", "plugins", "edvr_mfd", "plugin.dll")
    if not os.path.isfile(plugin_dll):
        raise FileNotFoundError(f"Built plugin DLL not found: {plugin_dll}. Run build.bat first.")

    packages_dir = os.path.join(repo_root, "packages")
    if not os.path.isdir(packages_dir):
        raise FileNotFoundError(f"Packages directory not found: {packages_dir}")

    # Discover packages
    target_pkgs = ["ship", "route", "exploration"]
    packages_to_include = []
    for pkg_id in target_pkgs:
        pkg_dir = os.path.join(packages_dir, pkg_id)
        if not os.path.isdir(pkg_dir):
            raise FileNotFoundError(f"Required package directory missing: {pkg_dir}")
        packages_to_include.append((pkg_id, pkg_dir))

    # Validate packages
    if not skip_validate:
        for pkg_id, pkg_dir in packages_to_include:
            ok, msg = validate_package(repo_root, pkg_dir)
            if not ok:
                raise RuntimeError(f"Package validation failed for '{pkg_id}':\n{msg}")

    # Prepare output dir
    output_zip_path = os.path.abspath(output_zip_path)
    os.makedirs(os.path.dirname(output_zip_path), exist_ok=True)

    # Build zip file
    items_added = []
    with zipfile.ZipFile(output_zip_path, "w", zipfile.ZIP_DEFLATED) as zf:
        # 1. Plugin DLL -> plugins/edvr_mfd/plugin.dll
        dll_arcname = "plugins/edvr_mfd/plugin.dll"
        zf.write(plugin_dll, dll_arcname)
        items_added.append(dll_arcname)

        # 2. Bundled packages -> plugins/edvr_mfd/mfds/<pkg_id>/...
        for pkg_id, pkg_dir in packages_to_include:
            for root, dirs, files in os.walk(pkg_dir):
                for f in sorted(files):
                    # Do not include temporary files or runtime logs
                    if f.endswith(".tmp") or f == "companion.log" or f.startswith("events."):
                        continue
                    full_p = os.path.join(root, f)
                    rel_p = os.path.relpath(full_p, pkg_dir).replace("\\", "/")
                    arcname = f"plugins/edvr_mfd/mfds/{pkg_id}/{rel_p}"
                    zf.write(full_p, arcname)
                    items_added.append(arcname)

        # 3. Schemas -> docs/schema/
        schema_dir = os.path.join(repo_root, "docs", "schema")
        if os.path.isdir(schema_dir):
            for root, dirs, files in os.walk(schema_dir):
                for f in sorted(files):
                    full_p = os.path.join(root, f)
                    rel_p = os.path.relpath(full_p, schema_dir).replace("\\", "/")
                    arcname = f"docs/schema/{rel_p}"
                    zf.write(full_p, arcname)
                    items_added.append(arcname)

        # 4. Root documentation & guides
        readme_path = os.path.join(repo_root, "README.md")
        if os.path.isfile(readme_path):
            zf.write(readme_path, "README.md")
            items_added.append("README.md")

        license_path = os.path.join(repo_root, "LICENSE")
        if os.path.isfile(license_path):
            zf.write(license_path, "LICENSE")
            items_added.append("LICENSE")

        authoring_guide_path = os.path.join(repo_root, "docs", "mfd-authoring-guide.md")
        if os.path.isfile(authoring_guide_path):
            zf.write(authoring_guide_path, "docs/mfd-authoring-guide.md")
            items_added.append("docs/mfd-authoring-guide.md")

        # 5. Native Windows auto-installer
        installer_exe = os.path.join(repo_root, "build", "tools", "install_mfd.exe")
        if os.path.isfile(installer_exe):
            zf.write(installer_exe, "install_mfd.exe")
            items_added.append("install_mfd.exe")

    zip_size = os.path.getsize(output_zip_path)
    print(f"[package_release] Successfully created {output_zip_path} ({zip_size} bytes, {len(items_added)} files)")
    return output_zip_path, items_added


def self_test():
    import tempfile
    with tempfile.TemporaryDirectory() as tmp:
        repo_root = os.path.join(tmp, "repo")
        dll_dir = os.path.join(repo_root, "build", "plugins", "edvr_mfd")
        os.makedirs(dll_dir, exist_ok=True)
        fake_dll = os.path.join(dll_dir, "plugin.dll")
        with open(fake_dll, "wb") as f:
            f.write(b"FAKE_PLUGIN_DLL")

        # Create mock packages: ship, route, exploration
        for pkg in ["ship", "route", "exploration"]:
            p_dir = os.path.join(repo_root, "packages", pkg)
            os.makedirs(os.path.join(p_dir, "screens"), exist_ok=True)
            os.makedirs(os.path.join(p_dir, "bin"), exist_ok=True)
            with open(os.path.join(p_dir, "manifest.json"), "w", encoding="utf-8") as f:
                f.write(f'{{"manifestVersion": 1, "id": "{pkg}", "version": "1.0.0"}}')
            with open(os.path.join(p_dir, "screens", "screen1.json"), "w", encoding="utf-8") as f:
                f.write('{"title": "SCREEN"}')
            with open(os.path.join(p_dir, "bin", f"edvr-mfd-{pkg}.exe"), "wb") as f:
                f.write(b"EXE_BYTES")

        # Mock docs/schema
        schema_dir = os.path.join(repo_root, "docs", "schema")
        os.makedirs(schema_dir, exist_ok=True)
        with open(os.path.join(schema_dir, "display.v1.schema.json"), "w", encoding="utf-8") as f:
            f.write('{"$schema": "http://json-schema.org/draft-07/schema#"}')

        # Mock README.md, authoring guide
        with open(os.path.join(repo_root, "README.md"), "w", encoding="utf-8") as f:
            f.write("# EDVR MFD")
        guide_dir = os.path.join(repo_root, "docs")
        os.makedirs(guide_dir, exist_ok=True)
        with open(os.path.join(guide_dir, "mfd-authoring-guide.md"), "w", encoding="utf-8") as f:
            f.write("# Authoring Guide")

        out_zip = os.path.join(tmp, "dist", "edvr-mfd-v1.0.0.zip")
        # Test creating release zip (skip_validate for unit mock test)
        create_release_zip(repo_root, out_zip, skip_validate=True)
        assert os.path.isfile(out_zip)

        # Verify zip contents
        with zipfile.ZipFile(out_zip, "r") as zf:
            namelist = set(zf.namelist())
            assert "plugins/edvr_mfd/plugin.dll" in namelist
            assert "plugins/edvr_mfd/mfds/ship/manifest.json" in namelist
            assert "plugins/edvr_mfd/mfds/ship/screens/screen1.json" in namelist
            assert "plugins/edvr_mfd/mfds/ship/bin/edvr-mfd-ship.exe" in namelist
            assert "plugins/edvr_mfd/mfds/route/manifest.json" in namelist
            assert "plugins/edvr_mfd/mfds/exploration/manifest.json" in namelist
            assert "docs/schema/display.v1.schema.json" in namelist
            assert "README.md" in namelist
            assert "docs/mfd-authoring-guide.md" in namelist

    print("[package_release] self-test: PASSED")
    return 0


def main():
    parser = argparse.ArgumentParser(description="Package EDVR MFD Release Artifact")
    parser.add_argument("--version", default="1.0.0", help="Release version (default: 1.0.0)")
    parser.add_argument("--output", "-o", help="Output zip path (default: dist/edvr-mfd-v<version>.zip)")
    parser.add_argument("--skip-validate", action="store_true", help="Skip mfdkit validate check")
    parser.add_argument("--self-test", action="store_true", help="Run internal self-tests and exit")
    args = parser.parse_args()

    if args.self_test:
        return self_test()

    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    out_path = args.output
    if not out_path:
        out_path = os.path.join(repo_root, "dist", f"edvr-mfd-v{args.version}.zip")

    try:
        create_release_zip(repo_root, out_path, skip_validate=args.skip_validate)
        return 0
    except Exception as e:
        print(f"[package_release] ERROR: {e}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
