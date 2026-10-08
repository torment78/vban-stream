# OBS Program output in VBAN Stream 0.3.5

The third settings tab, **OBS to VBAN Frame**, sends the main OBS Program picture
as JPEG or PNG over UDP. See the [setup instructions](../README.md#send-obs-program-to-vban-frame).
This is one outgoing video stream, independent of the two incoming videos, mouse
routes and audio returns. Preview and capture of the OBS interface are not implemented.

## Capture and bounded work

`ProgramOutput` registers the public OBS raw-video callback with RGBA conversion
and an FPS divisor. OBS skips unneeded frames before CPU scaling/conversion. The
callback also gates by OBS timestamp for rates that are not integer divisors.
OBS scales its normal output to fit 640 x 360, 1280 x 720 or 1920 x 1080, preserving
aspect ratio without upscaling. This captures composited SDR Program, including
transitions, with or without Studio Mode. PQ/HLG is rejected; audio is excluded.

The callback only copies selected rows into preallocated buffers under a try-lock;
a busy queue causes a counted skip. Three fixed RGBA buffers (about 24 MiB at
1080p) hold the current encode, latest pending capture and previous encoded pixels.
The encoder compares pixels exactly. Unchanged pictures skip encoding and sending,
with a cached full-image refresh roughly once a second for late/restarted receivers.

JPEG/PNG encoding and UDP transmission run on separate workers. Encoding the next
image overlaps packet pacing for the current one. There is at most one pending raw
capture and one pending encoded image; newer work replaces stale pending work. The
current image transmission completes before starting another. No image encoding,
allocation, network I/O or pacing wait runs in an OBS video/audio callback.

The wrapper removes the OBS callback before stopping the workers. Stop predicates
and condition-variable waits share a mutex; shutdown wakes both workers, interrupts
pacing, joins them and closes the socket. An in-progress image encode must finish
before joining. An enabled raw-video consumer makes OBS video active; disable this
output and Apply before changing OBS video settings or profiles.

## Image encoding

Adaptive JPEG is optional and defaults on. The configured quality is its ceiling.
A target of 85% of the per-frame network budget allows margin for scheduling. The
worker starts at its last adaptive quality, with at most two lower-quality retries
when too large. Minimum quality is 5 (or the configured ceiling if lower). Quality
rises gradually when comfortably under budget. If an image still cannot fit, FPS
may fall; this is not a guaranteed constant-rate codec. Fixed-quality mode uses
the requested quality unchanged. PNG remains lossless, with faster compression.
JPEG uses optimized Huffman tables to reduce the payload without changing its
quantization or decoded pixels at the same quality setting. This remains a standard
JPEG, not progressive JPEG or a new video codec. Both codecs run on the CPU.
No GPU video codec is used.

## Wire protocol and pacing

The [VBAN specification](https://vb-audio.com/Voicemeeter/VBANProtocol_Specifications.pdf)
defines VBAN-Frame's 28-byte header and image fragmentation. This sender uses:

- Protocol `0x80`, with the declared rate index for 6, 12, 24, 48 or 84 Mbps.
- A 16-bit little-endian fragment index in bytes 5–6.
- First/continuation/last flags 1/2/4, or 5 for a single-fragment image.
- A zero-padded 16-byte stream name and little-endian image counter in bytes 24–27.
- Up to 1436 image bytes per packet, for a maximum UDP payload of 1464 bytes.

The encoder produces standard opaque JPEG or PNG images. The shared image counter
continues across destination changes. No private image format or compression
dependency is needed on the receiver beyond ordinary JPEG/PNG decoding.

The network limit includes a conservative allowance for packet overhead.
Transmission paces each individual packet from the preceding packet's completed socket
write, so a delayed worker never dumps catch-up bursts. Each picture is spread over
at least 85% of its configured frame interval; larger pictures take longer to obey
the bandwidth cap. The final packet is paced too, followed by a decode gap of
one eighth of the image wire time at that cap, clamped to 2–25 ms. Larger images
therefore leave more time for receivers that decode on their UDP-reading thread.
Windows uses a cancellable high-resolution waitable timer for waits over 1 ms,
then yields through the remaining sub-millisecond interval. This avoids rounding
every short packet wait up and lowering FPS; the precision tail costs video-worker
CPU time. macOS uses an interruptible condition-variable wait for the bulk of longer waits
and a bounded, non-yielding precision tail on the video sender thread. This can
consume one CPU core while short packet gaps are being timed. A native large-PNG
test exposed excessive
per-packet wait rounding without that tail. Version 0.3.5 also rebuilds
the universal Mac package; native Mac playback still requires a live test. Neither changes global timer resolution or audio
worker priority.
This gap cannot compensate for every receiver's decode speed or socket capacity.
An image exceeding one second's network budget is
rejected before any packet is sent. Transmission also has a 1.5-second deadline,
below the plugin receiver's two-second incomplete-image timeout. A failed send
abandons that image and counts an error; the next image can recover.

## Configuration and diagnostics

The optional `frame_output` settings object defaults to disabled, automatic local
routing, `OBS-PROGRAM`, UDP 6980, JPEG, 1280 x 720, 15 fps, maximum quality 80, adaptive JPEG and 24 Mbps.
The destination IPv4 address must be set before enabling. Preparing a configuration
opens its socket and allocates buffers but sends no packets. Invalid values or an
unavailable explicit adapter leave the existing configuration running.

Status reports the actual local IPv4 address, dimensions, completed images,
packet/byte totals, skipped frames and errors. A bounded history measures completed
images and estimated wire bytes over the last second for FPS/Mbps. Diagnostics
also show unchanged captures, actual JPEG quality, latest encode and paced-send
times, and capture-to-send age. This age ends at the last UDP write and excludes
remote reception/decoding. Idle refresh naturally reports about 1 FPS. Intentional
FPS throttling and unchanged suppression are separate from error/drop counts.
These are local counters; UDP does not confirm remote display. No firewall, router
or remote audio settings are changed.

## Verification and remaining acceptance testing

- `vban-frame-output`: isolated UDP receivers verify exact headers, image counters,
  JPEG/PNG pixels, FPS gating, route changes, unavailable adapters, fragments above
  index 255, paced large PNGs, over-budget rejection without partial transmission,
  adaptive/fixed JPEG quality, measured status, bounded pending work and shutdown.
  A quality-100 network payload is smaller than an unoptimized JPEG of the same
  picture and produces exactly the same decoded RGB pixels.
- `vban-frame-idle-bandwidth`: a static picture sends only periodic complete refreshes
  over three seconds, with the estimated wire rate below its cap.
- `obs-dll-smoke`: the three settings tabs, nested mouse cards, disabled defaults
  and persisted output configuration.
- `obs-video-smoke32`: the distributed Windows plugin runs against OBS 32.2.1 and
  D3D11. A real rendered Program scene is captured, sent, reassembled and decoded
  over isolated UDP, with a pixel check after switching the Program source.
- Existing receive, mouse and monitor-return suites and channels 1–8 remain required.

These checks establish local capture and packet/image behavior. A real VBAN-Screen
session, sustained LAN performance alongside audio, and native OBS interaction on
Windows and Mac still require user testing before stable promotion.

## Local performance comparison

`frame-performance --report-only` generates a repeatable detailed 720p/1080p image
with a changing frame marker at 30 FPS. A separate localhost receiver reassembles
and decodes JPEGs. This isolates sender/codec work from LAN and VBAN-Frame receiver
performance; it is not a visual quality or remote-device benchmark.

On the same Windows development PC, the 0.3.1 sender delivered about 19.7 FPS at
1080p, quality 25, 84 Mbps cap. The 0.3.2 pipeline delivered about 29.3 FPS at the
same quality. Wire traffic rose from 42.6 to 63.8 Mbps because it delivered more
pictures. With adaptive quality enabled at a ceiling of 80, moving 720p and 1080p
both reached about 29.7 FPS at actual qualities 28 and 22; these are reduced-quality
comparisons. A static 720p image fell from about 40 Mbps to around 1 Mbps.

The automatic test asserts bounded idle traffic and recovery refreshes, not a
hardware-specific moving-scene FPS threshold. Sustained performance and actual
VBAN-Frame display smoothness must still be checked on the user's LAN.

## 0.3.3 packet-loss reproduction

`frame-performance --high-quality` sends a deliberately detailed 720p picture at
fixed JPEG 100, with a changing marker, a 30 FPS capture request and an 84 Mbps cap.
It repeats with 1 MiB, 64 KiB and 8 KiB receiver socket buffers. The final 8 KiB case
only drains/reassembles packets, isolating transport from inline JPEG decoding.
These are diagnostic runs, not hardware-dependent pass/fail FPS assertions.

On the Windows development PC, the old 16-packet/catch-up sender completed 19 of
19 sent pictures with a 1 MiB buffer but only 10 of 19 with 64 KiB and one of 19
with 8 KiB. All reported zero local send errors. This reproduced the distinction
between successful UDP writes and successfully received pictures without a LAN.

The new pacing and optimized JPEG completed 19 of 19 with 64 KiB at about 60 Mbps,
compared with about 83 Mbps before. With only 8 KiB and inline decoding, ten of 19
still completed; draining without decoding completed all 19. A sufficiently small
socket can still overflow while its receiving thread decodes. The deliberately
complex quality-100 images remain too large for 30 FPS within the selected cap.

The normal adaptive 720p/1080p runs retained about 29.7 FPS. Fixed ceiling 25 at
1080p used about 54 Mbps, compared with about 64 Mbps in the earlier 0.3.2 run.
These local measurements do not establish sustained VBAN-Screen behavior on the
user's network; the new installer requires that separate reception test.

Version 0.3.3 is a Windows-only test release. Mac builds run manually; the published
Mac installer stays at 0.3.2 until the Windows functionality is settled.

## 0.3.4 LAN packet-order diagnosis

A 20-second capture of the reported 720p/JPEG-40 stream received about 11.35 Mbps.
The same packet stream was fed to the plugin's strict in-order assembler and a
separate diagnostic assembler that sorts fragment indices. The strict path
completed 204 pictures (10.2 FPS); sorting completed 571 (28.5 FPS). There were
1,597 nonconsecutive arrival transitions, including `19,16,17,18` within a burst.
All 571 reconstructed images decoded in both Qt and Windows GDI+, with no decode
errors or decoder-queue drops. The mean combined decoding time was about 5.1 ms.

These results identify packet ordering as a concrete failure on the tested path.
They do not identify whether the sender host, a NIC/driver, the network, or the
receiving host reordered the traffic, nor establish VBAN-Screen's internal behavior.
The diagnostic assembler closes at the end fragment, so its incomplete count can
include late packets; it is not proof that every such packet was lost on the wire.

Version 0.3.4 replaces four-packet bursts with individual-packet spacing. Local
moving 720p/1080p checks delivered 29.3-29.7 FPS with no incomplete pictures; this
is a candidate mitigation, not confirmation of a fix on the LAN. Repeat the
VBAN-Screen playback/error check with the new Windows installer before promotion.

The optional Windows `frame-probe <port> <seconds> <sender-ip>` build target listens
on an exclusive UDP port for at most 60 seconds. Close other listeners using that
port first. It records counters, sampled header bytes and fragment arrival indices,
never pictures, and changes no firewall or routing settings. The probe compares
strict/sorted assembly and Qt/GDI+ decoding. It is a developer tool, not part of
the installer. Read source and destination addresses in the correct direction.

The large-image pause was checked against the fixed-quality-100 diagnostic too.
With individual spacing but only a 2 ms gap, a 64 KiB receiver completed 13 of 24
sent pictures. With the bounded size-dependent gap it completed 21 of 21; the
8 KiB inline-decoding receiver also completed 21 of 21. This is one local run,
not a guaranteed minimum receiver-buffer size. Those detailed JPEG-100 images
still used about 70 Mbps at only 7 FPS because each image is roughly a megabyte.
The normal adaptive 720p/1080p tests remained at 29.3–29.7 FPS with no incomplete
pictures. Fixed JPEG 100 is not a substitute for an inter-frame video codec.

## 0.3.5 development refresh

Version 0.3.5 rebuilds the 0.3.4 sender for Windows and universal Mac. The user
confirmed that the 0.3.4 Windows LAN test now has smooth frames, good responsiveness
and no glitches. That result is specific to the tested Windows setup. Mac uses
a different scheduling primitive and still needs sustained real-device playback.
The release remains a development pre-release; stable is not promoted.
