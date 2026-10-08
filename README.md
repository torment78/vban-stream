<p align="center">
  <img src="docs/images/social-preview.jpg" width="100%" alt="VBAN Stream by ElkaSoft for OBS Studio">
</p>

# VBAN Stream

**Eight VBAN audio inputs, two video inputs and two monitor-mix returns for OBS Studio.**

An independent plugin by **ElkaSoft** for OBS Studio.

Receive named VBAN streams with 1–8 channels each directly in OBS. Send the OBS monitoring mix back to
two computers, each with its own destination address, UDP port, and stream name.
The 0.3.1 pre-release receives two VBAN-Frame video feeds, such as a
VoiceMeeter App View or a remote display, and adds Ctrl-click mouse return from
OBS Program in Studio Mode, on Windows x64 and macOS.

[**Windows installer — stable 0.2.2**](https://github.com/torment78/vban-stream/releases/download/v0.2.2/obs-vban-audio-0.2.2-windows-x64-setup.exe)
 · [Windows installer — development 0.3.1](https://github.com/torment78/vban-stream/releases/download/v0.3.1/vban-stream-0.3.1-windows-x64-setup.exe)
 · [Mac installer — development 0.3.1](https://github.com/torment78/vban-stream/releases/download/v0.3.1/vban-stream-0.3.1-macos-universal-test.pkg)
 · [Download all banners](https://github.com/torment78/vban-stream/releases/download/v0.2.5/VBAN-Stream-artwork.zip)
 · [Installation guide](https://github.com/torment78/vban-stream/releases/download/v0.3.1/VBAN-INSTALL.txt)
 · [Report an issue](https://github.com/torment78/vban-stream/issues)

[![Donate on Ko-fi](https://img.shields.io/badge/Donate-Ko--fi-FF5E5B?logo=ko-fi&logoColor=white)](https://ko-fi.com/msffixit)

Free and open source under GPL-2.0-or-later. Donations support development and are optional.

[Choose from eight banners](docs/ARTWORK.md) — circuit or waves designs, horizontal or vertical, with or without the VoiceMeeter / VB-Audio credit.

## Download and install

Windows and Mac development builds are **0.3.1**. The latest stable version is **0.2.2** and retains the earlier VBAN Audio name. Tested with **OBS 31.1.1** and **OBS 32.2.1** on Windows x64.

Use the setup installer for both stable and development builds.

| Installer | Version and platform |
| --- | --- |
| [Windows stable setup](https://github.com/torment78/vban-stream/releases/download/v0.2.2/obs-vban-audio-0.2.2-windows-x64-setup.exe) | 0.2.2, Windows x64, standard or portable OBS |
| [Windows development setup](https://github.com/torment78/vban-stream/releases/download/v0.3.1/vban-stream-0.3.1-windows-x64-setup.exe) | 0.3.1 pre-release, Windows x64, standard or portable OBS |
| [Mac development installer](https://github.com/torment78/vban-stream/releases/download/v0.3.1/vban-stream-0.3.1-macos-universal-test.pkg) | 0.3.1 pre-release, Apple Silicon and Intel |

Source code and optional manual packages are available separately on GitHub.

### macOS test build

The [0.3.1 pre-release](https://github.com/torment78/vban-stream/releases/tag/v0.3.1)
is for testing on **macOS 13 or later**, with **OBS Studio 31.1.1 or later**.
The universal `.plugin` bundle contains both Apple Silicon and Intel code.

Use the [Mac setup installer](https://github.com/torment78/vban-stream/releases/download/v0.3.1/vban-stream-0.3.1-macos-universal-test.pkg). It installs into
`~/Library/Application Support/obs-studio/plugins`. Quit OBS before installation.
These test builds use ad-hoc signing and are not Apple notarized; the
[Mac installation and testing guide](https://github.com/torment78/vban-stream/releases/download/v0.3.1/INSTALL-MAC.txt) explains approval and
includes a listening checklist. Sustained audio and real OBS GUI testing are still
required before promoting Mac support to a stable release.

### Using the Windows installer

1. Close OBS, run **vban-stream-0.3.1-windows-x64-setup.exe**, and approve the Windows administrator prompt.
2. Choose **Standard OBS installation** (selected by default) or **Portable OBS**.
3. Check the detected standard OBS folder, or click **Browse** to choose your portable
   OBS root: the folder containing **bin**, **data**, and **obs-plugins**.
4. Click **Next**, review the paths, then **Install**. Restart OBS and open
   **Tools → VBAN Stream Settings**.

Both installer modes put the DLL and data inside the selected OBS folder. Standard
mode adds a Windows Installed apps entry; portable mode keeps its uninstaller in
the plugin's data folder. The installer does not install OBS itself or enable
OBS portable mode. Your existing scenes, profiles and VBAN settings are preserved.

Already using the root ZIP? Select the same OBS folder to update it. If you used
the ProgramData ZIP, remove that plugin copy before switching to the installer.
Setup checks the usual ProgramData location in standard mode to prevent duplicate
copies. Close all OBS instances before installing or uninstalling.

### Manual ZIP installation

Use **one** layout and keep only one installed copy. Close OBS before copying files.
The root ZIP is the simplest option if you already install plugins by merging folders.

For a normal installation, the root layout is:

```text
C:\Program Files\obs-studio\
  obs-plugins\64bit\obs-vban-audio.dll
  data\obs-plugins\obs-vban-audio\locale\en-US.ini
  data\obs-plugins\obs-vban-audio\vban-audio.png
```

For portable OBS, use the same layout inside its own root folder.
Restart OBS and open **Tools → VBAN Stream Settings**.
The ZIPs also include **VBAN-INSTALL.txt**.

Requires the Microsoft Visual C++ x64 runtime. Uses the OBS installation's own
libobs, frontend API and Qt libraries. Do not copy development dependency DLLs
into OBS. These builds are unsigned.

## Receive VBAN audio in OBS

1. Open **Tools → VBAN Stream Settings**.
2. Enter the sender computer's IPv4 address and the UDP listen port (default **6980**).
3. Enable the slots you need. Enter each exact, case-sensitive VBAN stream name.
   Use **Friendly name** for the label you want in OBS.
4. Click **Apply**. Configure the sender to send to this OBS computer and the same port.
5. Add **Sources + → VBAN Stream**, then select the configured stream.

**Channels** in VBAN Stream Settings shows a small number box for each receiving stream: **2** for stereo, **4** for four channels, or **8** for eight channels. **Input format** beside it shows the received PCM bit depth or floating-point format. These are the incoming values before OBS downmixing. **Receiving** counts live streams, not individual channels or OBS source copies. A waiting, disabled or stopped stream shows a dash instead of an old channel count.

Each named stream automatically accepts **1–8 audio channels**; there is no
channel-count selector. The eight slots are eight separate streams, each of which
can carry up to eight channels. One source remains one mixer fader.

To retain all eight channels through OBS, set **Settings → Audio → Channels → 7.1**
and use an output/recording format that supports it. With OBS set to **Stereo**,
OBS downmixes multichannel input to stereo. Seven-channel input uses a silent
eighth channel. The two monitor returns remain stereo.

The source appears as a normal OBS mixer fader. Default source names adopt the
friendly name; manually assigned names are preserved. Multiple sources can select
the same slot without creating extra network receivers.

One sender/name pair should occupy one enabled slot. Stream names allow 1–16
printable ASCII characters. The stream dropdown stays open while you select it.

## Receive VBAN video in OBS

1. On the sender, choose a **VBAN-Frame** output, **App View** or a display, and **JPEG** or **PNG**.
2. Send to the OBS computer's IPv4 address and its UDP listen port (default **6980**).
3. Open **Tools → VBAN Stream Settings → Video inputs**. Enable Video 1 and/or Video 2.
4. Enter each sender's IP and exact stream name. Give each input a friendly name, then **Apply**.
5. Choose **Sources + → VBAN Video** and select the configured input. Add a second source for the other view.

The two video inputs are separate from the eight audio slots. Both use the same
UDP listen port. Status shows **Receiving**, resolution and JPEG/PNG after a
complete picture has decoded. Formats are detected automatically; images retain
their original colours and PNG transparency. The settings window uses a dark theme.

Video is displayed at its native resolution, up to a 4096 × 2160 image or the same
pixel count in portrait orientation. Before reception, a source shows a black
1280 × 720 picture. After three seconds without a complete frame it goes black at
its last dimensions, then recovers automatically when reception resumes.

Each video source is video-only. Add **VBAN Stream** for accompanying audio.
The **Mouse return** tab adds optional VoiceMeeter App View control.

Hover over a video's status for incomplete frames, decode errors and queue drops.
If these rise steadily, reduce the sender's frame rate/resolution, try JPEG, and
check the LAN. Incomplete images are dropped without displaying partial pictures.
[Protocol and testing details](https://github.com/torment78/vban-stream/blob/dev/docs/VIDEO-RECEIVE.md).

## Control VoiceMeeter from Program

1. Receive the sender's **App View** through a VBAN Video source and place it in your Program scene.
2. On that computer, enable an incoming **VBAN-TEXT** stream. Use the OBS computer's
   sender IP, choose a command stream name (for example **Command1**), and enable
   **Manage Mouse command** from the stream's format/sample-rate context menu.
3. Open **Tools → VBAN Stream Settings → Mouse return**. Enable the return for
   **Video 1** or **Video 2**, enter that computer's destination IP, its VBAN UDP
   listen port, and the exact incoming command stream name. Click **Apply**.
4. Turn on **Studio Mode**. Hold **Ctrl** and left-click, right-click or drag inside
   the live App View in the **Program** pane. On Mac, use the physical **Control** key.
   The Preview pane remains available for arranging the scene.

Each video has its own mouse destination. The command stream name is separate
from the video stream name. Use **Send from this PC** to select a LAN adapter if
automatic routing chooses the wrong one. Mouse return starts disabled.

Normal scene positioning, scaling, manual cropping, rotation, groups and nested
scenes are supported. An overlapping visible source blocks clicks through it;
filters and **Crop to Bounding Box** disable interaction on that source. Use App
View at its original sender size so the returned pixel coordinates match.

Release Ctrl, press Esc or leave the picture to end a drag. Changing scenes,
leaving Studio Mode, losing focus or losing the video also releases the remote
button. Keyboard typing, the mouse wheel, projectors and fullscreen Program are
not supported. **Sent** counts outgoing UDP packets, not confirmed reception.

Mouse return is a preview feature: please test it with your VoiceMeeter version
before using it during a live show. [Setup and implementation details](docs/MOUSE-RETURN.md).

## Send the monitor mix back

1. Open **VBAN RETURNS** in the same settings window.
2. Under **Send from this PC**, select this OBS computer's LAN address and adapter.
   **Automatic** lets Windows select the route.
3. Enable Return 1 and/or Return 2. Choose **PCM 16-bit** or **PCM 24-bit** for each
   return, then enter its destination IPv4 address, incoming VBAN port, and stream name.
4. Leave **Return audio buffer** at **60 ms** initially, then click **Apply**.
5. In OBS **Advanced Audio Properties**, use **Monitor Only** or **Monitor and Output**
   for sources you want in the return. **Monitor Off** excludes a source.
6. In VoiceMeeter, enable an incoming VBAN stream with the matching name, port,
   and sender IP (the OBS computer).

Both returns carry the same stereo monitor mix, including source filters and fader
gains, encoded at their independently selected PCM bit depths. Existing settings
retain **PCM 24-bit**. Changing bit depth does not change the sample rate: with OBS
set to **48 kHz**, both returns remain at **48 kHz**. Each has an independent socket
and packet counter.

In tested Windows OBS 32.2.1, the normal program mute button does not mute headphone
monitoring, and the return follows that behavior. Use **Monitor Off** to remove
a source from the return. OBS 31.1.1 honors the monitoring capture mute flag.

Returns send silence when no monitored source contributes. **Sending** means
Windows accepted outgoing packets; it does not confirm remote reception.
The applied source and destination addresses appear beside each return.
An unavailable selected adapter produces an error instead of silently switching.

**Avoid feedback:** do not feed either received return into a VBAN stream that
comes back into OBS.

## Buffering and clicks

The return buffer is adjustable from **20 to 200 ms**, default **60 ms**.
Increasing it gives delayed source callbacks more time to arrive, at the cost of
additional monitor delay. Try **100 ms** if **Late audio frames** keeps increasing
during steady playback. VoiceMeeter's incoming network buffer is a separate setting.

Version 0.2.2 also doubles capture capacity to 128 blocks per monitored source and
corrects small source-clock differences through gradual interpolation. It fixes
gaps/overlaps at the former 1 ms clock-rounding threshold. The worker requests
Windows multimedia audio scheduling priority.

Hover over a return's status to see:

| Counter | What it tells you |
| --- | --- |
| Capture queue overflows / queue peak | Source capture is outrunning the return worker. |
| Late audio frames | Source audio arrived after its mixing deadline. |
| Clock corrections | Small drift adjustments; increasing normally is expected. |
| Clock discontinuities | Source timestamps jumped or restarted. |
| Clipped samples | The combined mix exceeded full scale. Lower source faders. |
| Invalid float samples | A source/filter supplied non-finite audio. |
| Socket errors | Windows rejected an outgoing packet. |
| Send gaps over 20 ms / largest gap | The local sender paused or was delayed. |

Compare changes during steady playback. Startup, source changes, and Apply can
affect counters. Audio counters follow current source lifetimes; send/clipping
counters reset when return settings are applied. A quiet upstream source is valid
silence, so these counters cannot identify every audible problem.

If local counters stay steady but VoiceMeeter reports reception errors, check the
network and receiver, including its VBAN network-quality/buffering setting.
Clean local counters alone do not prove packet delivery. The plugin does not
change firewall, VPN, router, or audio-device settings.

## Supported input audio

Unsigned PCM8; signed PCM16/24/32; Float32 and Float64; one to eight channels.
Seven channels are padded to eight for OBS's 7.1 layout. Channel order is preserved.
Compressed/non-audio VBAN, invalid payloads, non-finite floats, and more than eight
channels are rejected. Packet parsing checks the 28-byte header and 1464-byte limit.

The eight-input receiver retains its existing 30 ms jitter target and approximately
10 ms output blocks, independently of the new return buffer. It tracks lost,
reordered, duplicate, late, invalid and unsupported packets plus under/overruns.
Missing input packets can cause silence and clicks upstream of the return.
No buffer reconstructs audio that never arrives.

## Build in Visual Studio 2026

Install **Desktop development with C++**, **MSVC v145**, **Windows SDK 10.0.26100.0**
and **C++ CMake tools for Windows**. CMake 4.3 or newer is required.
No .NET/C# project or separate Qt installation is needed.

To build the current pre-release, clone the dev branch into a working folder, then run PowerShell there:

```powershell
git clone --branch dev https://github.com/torment78/vban-stream.git obs-vban-audio
cd obs-vban-audio
.\tools\build.ps1 -Package
```

The script finds Visual Studio 2026 (including Insiders), downloads pinned official
OBS/Qt dependencies on first configuration, builds with MSBuild, and runs tests.
The SDK baseline is OBS **31.1.1** and the **2025-07-11** dependencies (Qt **6.8.3**).

After initial configuration, open **build_x64\obs-vban-audio.slnx** in Visual Studio
2026. Select **RelWithDebInfo | x64**, then **Build → Build Solution**.
Alternatively open the repository as a CMake folder and choose **windows-x64**.

Outputs are in **build_x64\RelWithDebInfo** and **dist**.

To add the EXE installer, install [Inno Setup 6](https://jrsoftware.org/isinfo.php),
then run the following after the Visual Studio build/package step:

```powershell
.\tools\package-installer.ps1
```

This packages the same DLL as the root ZIP and verifies it matches the Visual
Studio build. See [installer build and testing notes](docs/INSTALLER.md).
Tests use isolated OBS configurations and local UDP receivers. They do not play
through physical speakers or change your normal OBS or VoiceMeeter setup.

```powershell
.\tools\run-tests.ps1
```

The optional OBS 32.2.1 compatibility host uses matching frontend headers and the
official runtime under **.deps**; **tools\run-tests-obs32.ps1** runs that check when
those optional files are available. They are not included in release downloads.
See the [audio review](docs/AUDIO-REVIEW-0.2.2.md) for validation and limitations.

Releases are built and tested locally with Visual Studio 2026. Inherited template
Inherited template build/release workflows remain disabled. The macOS workflow
builds a universal package on dev and validates it on Apple Silicon and Intel.
Windows installers are built locally with Visual Studio 2026.

## Support and credits

Use [GitHub Issues](https://github.com/torment78/vban-stream/issues) for bug reports.
Include OBS/plugin versions, sample rate, return buffer, and which counters increase.
Remove private addresses or other personal information from logs before posting.

[Donate on Ko-fi](https://ko-fi.com/msffixit), or use the Donate button in
**Tools → VBAN Stream Settings**.

Built on the [official OBS plugin template](https://github.com/obsproject/obs-plugintemplate)
and the [public OBS source API](https://docs.obsproject.com/reference-sources).
Implements the [VBAN PCM specification](https://vb-audio.com/Voicemeeter/VBANProtocol_Specifications.pdf)
directly; no VoiceMeeter Remote DLL is required.

Independent community project; not an official OBS Project or VB-Audio product.
GPL-2.0-or-later; see [LICENSE](LICENSE).
