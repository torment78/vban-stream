# VBAN Stream 0.3.0 — development preview

Display a VoiceMeeter App View or another VBAN-Frame video feed directly in OBS.
Configure two inputs in **Tools → VBAN Stream Settings → Video inputs**, then add
**VBAN Video** from the Sources menu and choose one of those inputs.

- Two independent video streams with automatic JPEG/PNG detection.
- Native image resolution up to 4K; received colours and PNG transparency preserved.
- Dark settings window and a black waiting picture when a sender is absent.
- Recovery after incomplete images, sender restarts and source-selection changes.
- Existing eight audio inputs (up to eight channels each) and both monitor returns.
- Existing audio settings and scene sources are retained on upgrade.

This is a pre-release for testing. Mouse/keyboard control is not included.
Use the Windows setup installer for standard or portable OBS, or the universal
Mac PKG for Apple Silicon/Intel. Close OBS before upgrading. macOS 13+ and
OBS 31.1.1+ are required; Mac packages remain ad-hoc signed and not notarized.

Start with one sender in JPEG mode at a modest resolution/frame rate, then enable
the second view. Match the sender IP, exact stream name and shared UDP listen port.
Check both pictures, stop/restart each sender, and listen to the existing audio
paths while video is running. Report sender/OBS versions and the video status
counters if reception fails or frames are skipped.

Support: https://discord.gg/AAhxYKzmkz
Website: https://elkasoft.xyz/
Donate: https://ko-fi.com/msffixit
