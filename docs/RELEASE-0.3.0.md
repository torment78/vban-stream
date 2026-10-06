# VBAN Stream 0.3.0 — development preview

**Installers:** [Windows setup](https://github.com/torment78/vban-stream/releases/download/v0.3.0/vban-stream-0.3.0-windows-x64-setup.exe) · [Mac universal PKG](https://github.com/torment78/vban-stream/releases/download/v0.3.0/vban-stream-0.3.0-macos-universal-test.pkg)

Manual binary ZIPs and the text installation guides are available below.

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

Validation: 13 automated tests on Windows; OBS 32.2.1 monitor-return, 1–8-channel audio and real D3D11 video-output checks; 53 Windows installer checks. Universal Mac builds run the protocol, UDP, module and audio tests on Apple Silicon and Intel. A real VoiceMeeter/Matrix LAN video session and sustained listening still need user testing.
