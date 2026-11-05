# Camera tab and local RTSP streaming

This change adds a **Camera** tab next to **Physical Printer** on PrusaSlicer
2.9.7 (`stable`). The existing Physical Printer tab provides PrusaLink.

## Setup and playback

Camera sources belong to **Physical Printer** profiles, independently of slicing
presets and print-host upload settings. In **Printers**, select a physical printer
and open its cog dialog. The **Camera** section provides:

- **No camera**: no source is assigned.
- **RTSP / IP camera**: enter the complete `rtsp://camera-host/stream` URL.
- **HTTP / MJPEG stream**: enter the direct `http://camera-host/stream` URL.
  This must be a multipart MJPEG feed, rather than an HTML camera page. HTTPS,
  HLS, WebRTC, and periodic snapshot polling are not implemented.
- **USB / local camera**: choose a video device attached to the computer running
  PrusaSlicer. **Refresh** discovers newly attached cameras. Disconnected saved
  devices remain visible as unavailable; another camera is never substituted.

Choose **OK** to save. **Remove camera** clears the camera assignment when the
settings are saved; **Cancel** discards edits. Print-host credentials and printer
presets are unaffected. The Camera tab's cog opens only camera settings for the
viewed physical printer, using the same form. The Camera header’s printer selector
switches between physical printers without changing the slicing preset or print
destination. A white dot marks printers with a configured camera; it does not
indicate whether the camera is online. Playback intent is remembered separately
for each printer during the session. Selecting a different physical printer elsewhere also updates the camera
view. Without a physical printer selection, the tab explains where to create or
select one.

For a Buddy3D/xBuddy camera, first enable local RTSP streaming in Prusa Connect
or the Prusa app. The computer must be able to reach the camera locally. The
camera may have a different address from the printer.

The earlier global RTSP setting is migrated once to the selected physical printer
if that printer has no camera; the global setting is then cleared. URLs containing
embedded credentials are rejected by the editor and are not migrated. Camera
addresses are saved in physical-printer profiles, so exported physical-printer
bundles also contain their camera settings. USB identifiers are local to a
computer and may need reselection after moving a profile to another computer.

The camera plays directly inside PrusaSlicer through statically linked FFmpeg.
No VLC installation, external player, subprocess, or local proxy is required.
Network sources support H.264, H.265, and MJPEG over RTSP/TCP, or MJPEG over HTTP.
Local capture uses AVFoundation on macOS, DirectShow on Windows, and V4L2 on
Linux. Only video is captured. macOS requests camera access on first capture;
its application bundle includes the camera usage description. Other systems
also require their normal camera permissions.

The preview has a slim header with its settings cog, a bottom control bar with
one animated Play/Stop button, and a Prusa-themed stopped screen with a centered
**Start stream** action. While connecting, the preview shows the rotating Prusa
spool, rendered natively in the current theme. Both playback controls can cancel
the connection. The spool stops on video, failure, cancellation, or tab hide.

Leaving the tab releases the connection while remembering playback intent for
that physical printer in session memory. Returning reconnects a source previously
started for that printer. Selecting a different printer stops the previous feed
and uses that printer's camera and session state. Explicit Stop, Cancel, or
removing the camera clears its intent. New application launches start stopped.
A disconnected or stalled feed restores the start screen and reports an error.

Prusa's camera documentation:
<https://help.prusa3d.com/article/buddy3d-camera_821264>.

## Implementation and build

The decoder opens and reads the stream on a worker thread. Network operations
have timeouts and an interrupt callback so Stop and destruction can cancel a
pending connection. Decoded frames are converted to a bounded latest RGB frame
buffer; all wxWidgets work remains on the UI thread. The preview is 640 × 360.
No credentials or camera URLs are written to decoder console logs.

Use PrusaSlicer's normal native dependency build followed by its application
build. `deps/+FFmpeg/FFmpeg.cmake` downloads the pinned FFmpeg 8.1.3 source,
verifies its SHA256, and builds only the required static video and RTSP
and local-device components. GPL/nonfree components and external codec dependencies are disabled.
The dependency configure step fails if FFmpeg disables the requested platform
camera backend. On macOS, AVFoundation is explicitly enabled even though
automatic optional-library discovery is disabled.

The application links `avdevice`, `avformat`, `avcodec`, `swscale`, and `avutil` using
`cmake/modules/FindFFmpeg.cmake`.

On Windows, building the FFmpeg dependency additionally requires a POSIX shell
and GNU make (MSYS2) in the compiler environment. On macOS and Linux, the standard
native tools are used. The runtime application does not require these tools.
The pinned AVFoundation backend has a small patch to honor cancellation while
waiting for its first frame. Without it, a denied or unplugged camera could
block shutdown. See `deps/+FFmpeg/avfoundation-interrupt.patch`.

FFmpeg notices and its LGPL license are included in `resources/licenses/FFmpeg`.

## Decoder integration test

After building the dependencies, configure the standalone test with the same
absolute dependency prefix:

```sh
cmake -S tests/video -B build/video-smoke -DCMAKE_PREFIX_PATH=/absolute/dependency/prefix
cmake --build build/video-smoke
python3 tests/video/generate_fixture.py build/video-fixture.y4m
# Use the file:// URI printed above, or a changing RTSP feed:
build/video-smoke/video_stream_decoder_smoke 'file:///absolute/path/video-fixture.y4m'
build/video-smoke/video_stream_decoder_smoke 'rtsp://camera-host/live'
```

The test checks unavailable inputs and error reporting, cancellation while
connecting, changing RGB frames, three playback/reconnect cycles, idempotent
Stop, stale frame removal, and destruction during active playback.

Verified on Apple Silicon with a physical Buddy3D camera: native playback,
connection cancellation, repeated reconnects, automatic resume after tab changes,
and explicit Stop remaining stopped on return. Windows/Linux runtime testing
and a notarized installer are not included.

### Additional checks

The decoder smoke executable accepts an optional source type after the address
(`rtsp`, `http`, or `usb`). A missing local device is checked on every run.
For a loopback multipart HTTP test, install Pillow in a test environment and run:

```sh
python3 tests/video/http_mjpeg_fixture.py /path/to/video_stream_decoder_smoke
```

The `[Camera]` case in `tests/libslic3r/test_camera_config.cpp` checks profile
isolation, serialization, legacy defaults, and removal without changing print
host settings. It can be run through the normal libslic3r tests or the small
`camera_config_tests` target with `SLIC3R_BUILD_TESTS=ON`.

## Contribution status

This implementation targets the 2.9.7 `stable` architecture. It has not been
ported to the 3.x `master` architecture. It is submitted as a draft for feedback
on scope and the FFmpeg dependency before that port and cross-platform testing.

Remaining validation includes Windows and Linux dependency builds, device
capture and cancellation on those platforms, and macOS Intel. The current RGB
preview is fixed at 640 × 360, so non-16:9 sources are stretched. Decoder error
details are currently English. These limitations must be addressed or accepted
before treating the feature as ready for release.
