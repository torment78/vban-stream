#!/usr/bin/env python3
"""Create/run a CI-only runtime kit to test the same packaged plugin on Intel."""
from pathlib import Path
import hashlib
import json
import os
import platform
import plistlib
import subprocess
import sys
import tarfile

ROOT=Path(__file__).resolve().parents[1]
TESTS=("vban-tests","return-tests","obs-smoke","obs-return-smoke","obs-multichannel")

def run(*args,**kwargs):
    return subprocess.run([str(a) for a in args],check=True,**kwargs)

def create():
    paths=[ROOT/".deps/Frameworks"]
    paths += list((ROOT/".deps/lib").glob("*.dylib"))
    for dependency in (ROOT/".deps").glob("obs-deps-*-universal"):
        for sub in ("lib","plugins","share/obs"):
            path=dependency/sub
            if path.exists(): paths.append(path)
    hosts=[]
    for name in TESTS:
        path=ROOT/"build_macos/Release"/name
        if not path.is_file(): raise RuntimeError("Missing test host: "+str(path))
        run("codesign","--force","--sign","-","--timestamp=none",path)
        hosts.append(path)
    output=ROOT/"build_macos/test-kit.tar.gz"
    with tarfile.open(output,"w:gz") as archive:
        for path in paths+hosts:
            if not path.exists(): raise RuntimeError("Missing runtime: "+str(path))
            archive.add(path,arcname=str(path.relative_to(ROOT)))
    print("Created CI runtime kit:",output,output.stat().st_size)

def validate():
    if os.environ.get("GITHUB_ACTIONS") != "true":
        raise SystemExit("Validation requires a disposable GitHub Mac runner.")
    release=ROOT/"out/macos-candidate"
    for line in (release/"SHA256SUMS-macos.txt").read_text().splitlines():
        expected,name=line.split("  ",1)
        if Path(name).name != name: raise RuntimeError("Invalid manifest name")
        if hashlib.sha256((release/name).read_bytes()).hexdigest()!=expected:
            raise RuntimeError("Checksum failed: "+name)
    run("tar","-xzf",ROOT/"out/test-kit/test-kit.tar.gz","-C",ROOT)
    target=ROOT/"build_macos/validate"
    target.mkdir(parents=True,exist_ok=True)
    archive=next(release.glob("*-universal-test.zip"))
    run("ditto","-x","-k",archive,target)
    payload=target/"VBAN Stream"
    bundle=payload/"obs-vban-audio.plugin"
    for name,expected in json.loads((payload/"SHA256.json").read_text()).items():
        path=payload/name
        if not path.resolve().is_relative_to(payload.resolve()): raise RuntimeError("Invalid payload path")
        if hashlib.sha256(path.read_bytes()).hexdigest()!=expected: raise RuntimeError("Payload mismatch: "+name)
    run("codesign","--verify","--strict",bundle)
    with (bundle/"Contents/Info.plist").open("rb") as stream:
        executable=bundle/"Contents/MacOS"/plistlib.load(stream)["CFBundleExecutable"]
    run("lipo","-verify_arch","arm64","x86_64",executable)
    env=dict(os.environ)
    deps=ROOT/".deps"
    qt=deps/"obs-deps-qt6-2025-07-11-universal"
    minimal=next(qt.rglob("libqminimal.dylib"))
    env["QT_PLUGIN_PATH"]=str(minimal.parent.parent)
    env["DYLD_FRAMEWORK_PATH"]=str(deps/"Frameworks")+":"+str(qt/"lib")
    env["DYLD_LIBRARY_PATH"]=str(deps/"lib")+":"+str(deps/"obs-deps-2025-07-11-universal/lib")
    for name in TESTS:
        host=ROOT/"build_macos/Release"/name
        for channels in (range(1,9) if name=="obs-multichannel" else [None]):
            args=[host]
            suffix=name+("-"+str(channels) if channels else "")
            if name.startswith("obs-"):
                args += [executable,bundle/"Contents/Resources",target/(suffix+"-config")]
            if channels: args.append(str(channels))
            with (target/(suffix+".log")).open("w") as log:
                run(*args,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=120)
    (target/"RESULT.txt").write_text("PASS on "+platform.machine()+": released ZIP hashes, universal slices, bundle signature, UDP receive/return tests, OBS host loading/UI tests, monitor returns, incoming channels 1-8 in PCM16/24.\n")
    print((target/"RESULT.txt").read_text())

if __name__=="__main__":
    if sys.platform!="darwin":raise SystemExit("This script runs on macOS.")
    {"create":create,"validate":validate}[sys.argv[1]]()
