# VBAN Stream 0.3.6 — stable release

The settings window can be made smaller and scrolled, so its action buttons remain
accessible on smaller displays or when Windows scaling is increased.

The four tabs are **VBAN Audio**, **VBAN Returns**, **Video inputs** and
**OBS to VBAN Frame**. Every page supports vertical and horizontal scrolling when
needed. The two mouse-control cards remain beneath their matching video inputs.
The window opens at a size fitted to the current screen's available space.

Existing audio, return, video and mouse settings are preserved. Apply saves all
four tabs, OK saves and closes, and Cancel discards unapplied edits.

## Install or update

Close OBS and run **vban-stream-0.3.6-windows-x64-setup.exe**. Choose the same
standard or portable OBS folder as your current installation, then reopen OBS.

1. Open **Tools → VBAN Stream Settings**.
2. Make the window narrower and shorter, then visit all four tabs.
3. Scroll to the final input/return controls and check that OK, Apply and Cancel
   stay visible. Use the tab-bar arrows if the tab titles do not all fit.
4. Switch between tabs before applying an edit and check that the edit is retained.
5. Try the display/scaling setting that previously placed the buttons off screen.

Windows: Authenticode signed and timestamped plugin DLL, installer and uninstaller.
Mac: stable universal installer for Apple Silicon and Intel; unsigned and not notarized.
The Mac plugin carries an ad-hoc signature, not an Apple Developer ID signature.

Windows requires 64-bit OBS Studio 31.1.1 or later. Mac requires macOS 13 or later
and OBS Studio 31.1.1 or later. Close OBS before updating.

This release retains eight audio inputs (1–8 channels each), two stereo monitor
returns, two VBAN Frame inputs, two mouse-control routes and OBS Program output.
Artwork and audio/video/network processing are unchanged from 0.3.5.
