# OBS Program output in VBAN Stream 0.3.1

The third settings tab, **OBS to VBAN Frame**, sends the main OBS Program picture
as JPEG or PNG over UDP. See the [setup instructions](../README.md#send-obs-program-to-vban-frame).
This is one outgoing video stream, independent of the two incoming videos, mouse
routes and audio returns. Preview and capture of the OBS interface are not implemented.

## Capture and bounded work

`ProgramOutput` registers a public OBS raw-video callback with RGBA conversion.
OBS scales its normal output to fit 640 x 360, 1280 x 720 or 1920 x 1080, without
upscaling or changing aspect ratio. This captures the composited Program output,
including transitions, with or without Studio Mode. It excludes audio. PQ/HLG
video is rejected: this first sender requires SDR.

The callback uses OBS timestamps to limit capture to 1–30 frames/sec. It copies
selected rows into preallocated buffers, using a try-lock: a busy queue causes
a counted skip. There are three fixed RGBA buffers (at most about 24 MiB at
1920 x 1080), one frame being encoded and at most one pending frame. New pictures
replace pending old pictures. Encoding, allocations, socket sends and pacing
waits run on a separate worker, never on audio callbacks or in the video callback.

The wrapper disconnects the OBS callback and waits for in-flight capture to finish
before changing or destroying the worker. Shutdown interrupts pacing, joins the
worker and closes its socket. The worker's image encode must finish before joining.
An enabled raw-video consumer makes OBS video active; users must disable this
output and Apply before changing OBS video settings or profiles.

## Wire protocol and pacing

The [VBAN specification](https://vb-audio.com/Voicemeeter/VBANProtocol_Specifications.pdf)
defines VBAN-Frame's 28-byte header and image fragmentation. This sender uses:

- Protocol `0x80`, with the declared rate index for 12, 24, 48 or 84 Mbps.
- A 16-bit little-endian fragment index in bytes 5–6.
- First/continuation/last flags 1/2/4, or 5 for a single-fragment image.
- A zero-padded 16-byte stream name and little-endian image counter in bytes 24–27.
- Up to 1436 image bytes per packet, for a maximum UDP payload of 1464 bytes.

The encoder produces standard opaque JPEG or PNG images. The shared image counter
continues across destination changes. No private image format or compression
dependency is needed on the receiver beyond ordinary JPEG/PNG decoding.

The network limit includes a conservative allowance for packet overhead.
Transmission uses batches of at most 16 packets followed by interruptible pacing.
The final batch is paced too. An image exceeding one second's network budget is
rejected before any packet is sent. Transmission also has a 1.5-second deadline,
below the plugin receiver's two-second incomplete-image timeout. A failed send
abandons that image and counts an error; the next image can recover.

## Configuration and diagnostics

The optional `frame_output` settings object defaults to disabled, automatic local
routing, `OBS-PROGRAM`, UDP 6980, JPEG, 1280 x 720, 15 fps, quality 80 and 24 Mbps.
The destination IPv4 address must be set before enabling. Preparing a configuration
opens its socket and allocates buffers but sends no packets. Invalid values or an
unavailable explicit adapter leave the existing configuration running.

Status reports the actual local IPv4 address, image dimensions, completed sent
images, packet/byte totals, skipped frames and errors. Intentional FPS throttling
is not a skipped-frame error. These are local counters: UDP does not establish
that the receiver received or displayed an image. No firewall, router or remote
audio settings are changed.

## Verification and remaining acceptance testing

- `vban-frame-output`: isolated UDP receivers verify exact headers, image counters,
  JPEG/PNG pixels, FPS gating, route changes, unavailable adapters, fragments above
  index 255, paced large PNGs, over-budget rejection without partial transmission,
  bounded pending work and shutdown.
- `obs-dll-smoke`: the three settings tabs, nested mouse cards, disabled defaults
  and persisted output configuration.
- `obs-video-smoke32`: the distributed Windows plugin runs against OBS 32.2.1 and
  D3D11. A real rendered Program scene is captured, sent, reassembled and decoded
  over isolated UDP, with a pixel check after switching the Program source.
- Existing receive, mouse and monitor-return suites and channels 1–8 remain required.

These checks establish local capture and packet/image behavior. A real VBAN-Screen
session, sustained LAN performance alongside audio, and native OBS interaction on
Windows and Mac still require user testing before stable promotion.
