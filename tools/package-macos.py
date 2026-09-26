#!/usr/bin/env python3
"""Package the tested OBS bundle; no installation or system security changes."""
from pathlib import Path
import hashlib
import json
import plistlib
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = json.loads((ROOT / "buildspec.json").read_text())
VERSION = SPEC["version"]
NAME = SPEC["name"]
BUNDLE_ID = SPEC["platformConfig"]["macos"]["bundleId"]

def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], check=True, **kwargs)

def output(*args):
    return run(*args, capture_output=True, text=True).stdout.strip()

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    if sys.platform != "darwin":
        raise SystemExit("Packaging requires macOS.")
    source = ROOT / "build_macos/rundir/Release" / (NAME + ".plugin")
    if not source.is_dir():
        raise SystemExit("Build and test the Release bundle first.")
    destination = ROOT / "dist/macos"
    destination.mkdir(parents=True, exist_ok=True)
    staging = ROOT / "build_macos/packaging"
    staging.mkdir(parents=True, exist_ok=True)
    stage = Path(tempfile.mkdtemp(prefix="candidate-", dir=staging))
    payload = stage / "VBAN Stream"
    payload.mkdir()
    bundle = payload / source.name
    shutil.copytree(source, bundle, symlinks=True)
    with (bundle / "Contents/Info.plist").open("rb") as stream:
        plist = plistlib.load(stream)
    executable = bundle / "Contents/MacOS" / plist["CFBundleExecutable"]
    architectures = output("lipo", "-archs", executable).split()
    if set(architectures) != {"arm64", "x86_64"}:
        raise RuntimeError("Bundle must contain both Apple Silicon and Intel slices.")
    dependencies = output("otool", "-L", executable)
    for line in dependencies.splitlines():
        if not line.startswith("\t"):
            continue
        name = line.strip().split(" (", 1)[0]
        if not name.startswith(("@rpath/", "/System/Library/", "/usr/lib/")):
            raise RuntimeError("Nonportable dependency: " + name)
    run("codesign", "--force", "--sign", "-", "--timestamp=none", bundle)
    run("codesign", "--verify", "--strict", bundle)
    for name, source_name in (("INSTALL-MAC.txt", "docs/INSTALL-MAC.txt"), ("LICENSE", "LICENSE")):
        shutil.copy2(ROOT / source_name, payload / name)
    build_info = ("VBAN Stream " + VERSION + " macOS test build\nCommit: " + output("git", "-C", ROOT, "rev-parse", "HEAD")
        + "\nArchitectures: arm64 + x86_64\nMinimum macOS: 13.0\nOBS SDK: 31.1.1\n"
          "Signature: ad-hoc; not Developer ID signed or notarized.\n"
          "Real OBS GUI and sustained LAN listening tests are still required.\n\n" + dependencies + "\n")
    (payload / "BUILD-INFO.txt").write_text(build_info)
    manifest = {str(p.relative_to(payload)): digest(p) for p in sorted(payload.rglob("*")) if p.is_file() and not p.is_symlink()}
    (payload / "SHA256.json").write_text(json.dumps(manifest, indent=2) + "\n")
    archive = destination / ("vban-stream-" + VERSION + "-macos-universal-test.zip")
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", payload, archive)
    # OBS 31+ scans the user's Application Support directory on both architectures.
    pkgroot = stage / "installer"
    install_dir = pkgroot / "Library/Application Support/obs-studio/plugins"
    install_dir.mkdir(parents=True)
    shutil.copytree(bundle, install_dir / bundle.name, symlinks=True)
    components = stage / "components.plist"
    run("pkgbuild", "--analyze", "--root", pkgroot, components)
    with components.open("rb") as stream:
        contents = plistlib.load(stream)
    for component in contents:
        component["BundleIsRelocatable"] = False
        component["BundleIsVersionChecked"] = True
        component["BundleOverwriteAction"] = "upgrade"
    with components.open("wb") as stream:
        plistlib.dump(contents, stream)
    component_pkg = stage / "payload.pkg"
    run("pkgbuild", "--root", pkgroot, "--component-plist", components,
        "--identifier", BUNDLE_ID, "--version", VERSION, "--install-location", "/", component_pkg)
    distribution = stage / "Distribution.xml"
    distribution.write_text('''<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="1.0">
  <title>VBAN Stream - Mac Test Build</title>
  <options hostArchitectures="arm64,x86_64" customize="never" allow-external-scripts="no"/>
  <domains enable_currentUserHome="true" enable_anywhere="false" enable_localSystem="false"/>
  <allowed-os-versions><os-version min="13.0"/></allowed-os-versions>
  <welcome file="welcome.html" mime-type="text/html"/>
  <choices-outline><line choice="plugin"/></choices-outline>
  <choice id="plugin" title="VBAN Stream"><pkg-ref id="''' + BUNDLE_ID + '''"/></choice>
  <pkg-ref id="''' + BUNDLE_ID + '''" version="''' + VERSION + '''">#payload.pkg</pkg-ref>
</installer-gui-script>
''')
    resources = stage / "resources"
    resources.mkdir()
    (resources / "welcome.html").write_text("<html><body><h1>VBAN Stream</h1><p>by ElkaSoft</p>"
        "<p>Mac test build " + VERSION + " for OBS Studio 31.1.1 or later. macOS 13 or later.</p>"
        "<p>Quit OBS before installing. Installs for your current user in Library/Application Support/obs-studio/plugins.</p>"
        "<p>Eight incoming streams, up to eight channels per stream, and two stereo monitor returns.</p>"
        "<p>This test package uses ad-hoc signing and is not Apple notarized. Read INSTALL-MAC.txt before testing.</p>"
        "<p><a href=\"https://elkasoft.xyz/\">ElkaSoft website</a></p></body></html>")
    installer = destination / ("vban-stream-" + VERSION + "-macos-universal-test.pkg")
    run("productbuild", "--distribution", distribution, "--package-path", stage, "--resources", resources, installer)
    # Expand the final installer and compare the actual payload to the ZIP bundle.
    expanded = stage / "expanded"
    run("pkgutil", "--expand-full", installer, expanded)
    installed_bundles = list(expanded.rglob(bundle.name))
    if len(installed_bundles) != 1:
        raise RuntimeError("Unexpected installer bundle count.")
    installed = installed_bundles[0]
    for file in bundle.rglob("*"):
        target = installed / file.relative_to(bundle)
        if file.is_symlink():
            if not target.is_symlink() or file.readlink() != target.readlink():
                raise RuntimeError("Installer symlink mismatch: " + str(file))
        elif file.is_file() and (not target.is_file() or digest(file) != digest(target)):
            raise RuntimeError("Installer payload mismatch: " + str(file))
    run("codesign", "--verify", "--strict", installed)
    # Source remains available from the matching GitHub tag; do not duplicate it as a release asset.
    shutil.copy2(payload / "INSTALL-MAC.txt", destination / "INSTALL-MAC.txt")
    shutil.copy2(payload / "BUILD-INFO.txt", destination / "BUILD-INFO.txt")
    files = [archive, installer, destination / "INSTALL-MAC.txt", destination / "BUILD-INFO.txt"]
    (destination / "SHA256SUMS-macos.txt").write_text("".join(digest(p) + "  " + p.name + "\n" for p in files))
    print("Universal Mac installer, manual ZIP, payload, and signature verified:", destination)

if __name__ == "__main__":
    main()
