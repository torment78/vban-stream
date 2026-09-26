#!/usr/bin/env python3
"""Collect a backtrace of the isolated test host after a failed Mac test run."""
from pathlib import Path
import os
import subprocess
ROOT=Path(__file__).resolve().parents[1]
build=ROOT/"build_macos"
bundle=build/"Release/obs-vban-audio.plugin"
env_options=" ".join(name+"="+os.environ[name] for name in ("QT_PLUGIN_PATH","DYLD_FRAMEWORK_PATH","DYLD_LIBRARY_PATH") if name in os.environ)
command=["lldb","--batch","-o","settings set target.env-vars "+env_options,
         "-o","run","-k","thread backtrace all","--",str(build/"Release/obs-multichannel"),
         str(bundle/"Contents/MacOS/obs-vban-audio"),str(bundle/"Contents/Resources"),str(build/"backtrace-config"),"8"]
try:
    result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=90)
    (build/"crash-backtrace.log").write_text(result.stdout)
    print(result.stdout)
except subprocess.TimeoutExpired as error:
    output=error.stdout or b""
    if isinstance(output,bytes):output=output.decode(errors="replace")
    (build/"crash-backtrace.log").write_text(output+"\nDebugger timed out.\n")
    print(output)
