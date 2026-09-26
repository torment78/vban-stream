# VBAN Stream

Native Windows x64 OBS plugin: eight shared VBAN receive slots and two independent
monitor-mix returns. Build with Visual Studio 2026 and the windows-x64 CMake preset.

See README.md for installation/building, docs/MONITOR-RETURN-ENGINEERING.md for
architecture, and docs/AUDIO-REVIEW-0.2.2.md for validation and known limits.

## Release downloads

Development/pre-release and stable download buttons must link directly to the
platform installer: Windows `*-setup.exe` or macOS `.pkg`. Never use source ZIPs,
GitHub source tarballs, or a generic first-asset fallback as an install download.
Do not upload custom source archives; the repository and matching GitHub tags
provide source. Advanced manual packages may remain secondary GitHub assets.
Verify the installer asset exists before publishing a download link. Keep each
platform's stable and development links separate; a Mac-only pre-release must not
replace the Windows setup link. Update release checksums whenever assets change.
