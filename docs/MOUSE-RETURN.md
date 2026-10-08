# Mouse return in VBAN Stream 0.3.3

Mouse return links each of the two video inputs to a VBAN-TEXT destination.
It controls VoiceMeeter's App View from the Studio Mode Program pane in OBS.
See the [user setup steps](../README.md#control-voicemeeter-from-program).

## Configuration and compatibility

The dark Video inputs tab contains a Mouse control card directly beneath each
video's receive settings, with enabled/IP/port/stream-name fields and a shared
local adapter selector. The page scrolls when needed. Settings remain version 1: the optional
`mouse_returns` array and `mouse_local_ip` field default to disabled/automatic
when absent. Existing audio/video settings, source IDs and installed DLL names
are unchanged. Apply validates both mouse routes before committing any settings.
Duplicate enabled destination/IP/port/name routes are rejected to avoid competing
packet counters. Preparing a route sends nothing; replacing it releases a held
button to the old destination first.

The receiver must enable a matching incoming VBAN-TEXT stream and its **Manage
Mouse command** option. Mouse return does not enable that option remotely.
It does not change VoiceMeeter audio routing, the firewall, or the system mouse.
Use the sender's original App View size; arbitrary display video or a resized
sender image does not establish which application coordinates should be used.

## Wire protocol

The [VBAN specification](https://vb-audio.com/Voicemeeter/VBANProtocol_Specifications.pdf)
defines the 28-byte VBAN-TEXT header: protocol/rate `0x52`, unused/subchannel bytes
zero, UTF-8 `0x10`, a 16-byte name and a little-endian 32-bit packet counter.
Every datagram contains one complete command, without a trailing NUL:

```text
System.Mouse=(MOUSEMOVE, 120, 240);
System.Mouse=(LBUTTONDOWN, 120, 240);
System.Mouse=(LBUTTONUP, 120, 240);
```

`RBUTTONDOWN` and `RBUTTONUP` provide right-click. Command spelling and formatting
were checked against official VBAN-Screen 1.0.0.8 and the mouse-command example
in the [VoiceMeeter manual](https://vb-audio.com/Voicemeeter/VoicemeeterPotato_UserManual.pdf).
Ctrl is the local activation gesture; it is not forwarded as a remote modifier.
The sender uses nonblocking UDP sockets on the Qt UI thread, independent of
audio capture, mixing and video decoding. Movement is limited to 125 updates/sec;
button press and final release coordinates are sent immediately. Three idempotent
button-up packets reduce the chance of a lost release. UDP has no acknowledgement
or delivery guarantee; the local Sent counter is not proof of receiver acceptance.

## Program targeting

A Qt event filter discovers the single `OBSQTDisplay` under `previewLayout`
while Studio Mode is active. It excludes the editable `OBSBasicPreview`, and
fails closed if a future OBS UI changes that arrangement. No private OBS widget
layout or struct is cast. OBS 31/32's physical 10-pixel Program margin, DPI scaling
and letterboxing are removed before hit-testing the active output scene.

Hit-testing uses the inverse public scene-item draw transform and restores manual
crop offsets. It descends groups and nested scenes, chooses the top visible item,
and tracks scene identity, item path, video slot and native dimensions during a
drag. An overlapping source blocks clicks even if it has transparent pixels.
Filters and Crop to Bounding Box fail closed because they may change geometry.
Clicks require a currently receiving video with matching dimensions and an enabled
mouse route. Active transitions do not accept clicks.

Ctrl release, Esc, scene/collection changes, leaving Studio Mode, focus loss,
leaving the picture, video loss and route changes cancel the drag. A 50 ms timer
checks held targets between events; the receiver considers video stale after
three seconds without a decoded frame. macOS uses the physical Control key.
This implementation excludes keyboard typing, wheel/middle-button forwarding,
Preview, projectors, and fullscreen Program windows.

## Verification

- `mouse-tests`: exact packet framing and commands, two isolated UDP receivers,
  movement/buttons, route replacement release, invalid configuration, disabled
  defaults and duplicate destination rejection.
- `mouse-scene-tests`: real OBS transforms, scale/crop/rotation, overlays, hidden
  items, bounding crop rejection, high DPI and letterboxing.
- `obs-smoke`: actual plugin load, old-config defaults, saved routes and the two nested dark mouse cards.
- `obs-video-smoke32`: actual distributed DLL with OBS 32.2.1 and D3D11; two live
  PNG/JPEG streams plus synthetic Qt Program/Preview widgets. Internal Qt events
  verify Ctrl gating, independent destinations, drag/final coordinates, button-up
  on focus/Ctrl/scene changes, cross-target cancellation, right-click, Studio Mode
  gating and stale-picture rejection. No desktop input is injected.
- Existing audio receive/return and 1–8-channel regression suites remain required.

The isolated event fixture does not reproduce every detail of OBS's native window
delivery. Actual VoiceMeeter command acceptance and real OBS Program interaction
on Windows and Mac remain user acceptance tests; a passed build or UDP loopback
test alone does not establish them.
