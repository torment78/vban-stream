# VBAN Stream 0.2.6 - macOS test release

First macOS test build for OBS Studio. Includes the existing eight receive slots,
1-8 channels per incoming stream, and two stereo monitor returns with independent
PCM16/24 settings. Apple Silicon and Intel are included in one universal bundle.
Requires macOS 13+ and OBS Studio 31.1.1+.

## Downloads

- `vban-stream-0.2.6-macos-universal-test.pkg`: installs for your current Mac user.
- `vban-stream-0.2.6-macos-universal-test.zip`: manual installation of the same bundle.
- `INSTALL-MAC.txt`: setup, targeted macOS approval steps, and tester checklist.
- `BUILD-INFO.txt`: source commit, architectures and library dependencies.
- `SHA256SUMS-macos.txt`: checksums of all downloads.
- `vban-stream-0.2.6-macos-source.zip`: source used by the build.

The plugin uses an ad-hoc signature. The package is not Developer ID signed or
Apple notarized; macOS may require manual approval. Use the ZIP and the instructions
if the installer is blocked.

## Changes

- Native BSD sockets, IPv4 adapter enumeration and source-interface selection.
- Mac audio-worker scheduling and interruptible timing for monitor returns.
- Universal Mac build, per-user installer, bundle integrity checks, and portable tests.
- Socket buffer allocation adapts to the OS limit and reports its actual size.

Windows audio regressions are checked with Visual Studio 2026. Mac automated
validation covers UDP reception and returns, native OBS host loading and UI,
1-8 incoming channels, and PCM16/24. See the linked GitHub Actions run for the exact
results and architectures completed before publication.

Real OBS GUI use and sustained LAN listening remain tester work. This is a
pre-release, and the existing Windows 0.2.5 downloads are unchanged.

Please include Mac model/chip, macOS/OBS versions, network connection type, and
whether a problem affected incoming audio or a monitor return.

Website: https://elkasoft.xyz/  
Support: https://discord.gg/AAhxYKzmkz  
Donate: https://ko-fi.com/msffixit
