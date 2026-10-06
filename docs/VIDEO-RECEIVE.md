# VBAN-Frame video reception

Version 0.3.0 adds two video inputs configured in Tools → VBAN Stream Settings →
Video inputs and the separate `VBAN Video` OBS source (`vban_video_input`). Audio
source IDs and the existing settings path remain unchanged. Older settings load
with both video slots disabled. Settings continue to use version 1 with an optional
`videos` array.

## Protocol and threading

The receiver follows [VB-Audio's VBAN specification revision 13](https://vb-audio.com/Voicemeeter/VBANProtocol_Specifications.pdf),
pages 23–25. It accepts protocol 0x80, default subtype, a 16-byte ASCII stream name,
a little-endian 16-bit packet index and a 32-bit image counter. A datagram is at most
1464 bytes, including the 28-byte header. Start, continuation and end fragments
are reassembled; single-packet start+end and continuation+end are supported.

Matching requires an enabled slot, sender IPv4 and exact stream name. VBAN audio
and frame packets are demultiplexed before parsing, even when they share a name.
One UDP socket serves all inputs. The receive thread queues video packets and the
separate video worker reassembles and decodes them with Qt's JPEG/PNG readers.
Audio playback and return mixing do not decode images.

Only complete, valid images are published. A missing/out-of-order fragment discards
the current image; the next valid start recovers. Duplicate/older fragments cannot
replace newer pictures. Counters wrap naturally; a sender restart can recover after
two quiet seconds. No retransmission or video frame-rate conversion is attempted.

Limits: 4096 queued datagrams (about 6 MiB), 48 MiB per compressed image, two seconds
for partial assembly, maximum dimension 4096 and maximum pixel count 4096 × 2160.
Image type and dimensions are checked before decoding. Stale queued packets and
old configuration generations are ignored. Slow decoding or overload can drop
frames; these limits are not a guarantee of two simultaneous 4K/30 streams.

OBS pulls the latest decoded RGBA image on its video tick. Sources sharing a slot
share reception/decoding. A source is initially opaque black at 1280 × 720, uses
native incoming dimensions once available, and returns to black at its last size
after three seconds without a complete image. Received PNG alpha is preserved.

The dialog is dark, scoped to the plugin window. It does not change OBS's global
theme. This version does not send mouse/keyboard commands and does not advertise
OBS source interaction support.

## Verification

- `video-tests`: official wire framing, loss, duplicates, delayed old starts,
  counter wrap, timeouts and bounded assembly; two real loopback UDP streams with
  PNG/JPEG pixel checks, codec switching, invalid images, route edits, sender/name
  filters, same-name audio isolation, video-only mode and shutdown.
- `obs-smoke`: built plugin registration, source flags and picker, settings save,
  and screenshots of the new video tab alongside existing audio checks.
- `obs-video-smoke32` (Windows): the distributed DLL with official OBS 32.2.1,
  D3D11 rendering and captured output pixels; both streams, source switching,
  timeout-to-black and recovery. The host requests native BGRA output.
- Existing audio protocol, monitor returns and 1–8 channel tests remain required.
- Mac CI runs the protocol/UDP and module tests on Apple Silicon and Intel.

A real VoiceMeeter/Matrix sender over the user's LAN still needs testing. These
synthetic senders use the documented protocol; they cannot establish every sender
version's behaviour, LAN packet delivery or sustained production performance.
