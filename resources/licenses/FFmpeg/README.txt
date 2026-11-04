This application includes FFmpeg 8.1.3, licensed under the GNU Lesser General
Public License, version 2.1 or later. See COPYING.LGPLv2.1 in this directory.

Source: https://github.com/FFmpeg/FFmpeg/tree/n8.1.3
Source archive: https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n8.1.3.tar.gz
SHA256: 09f990289327d3ebfedf4fc15cae3f884a65dab7a5aac7bbb5b01ff83900efd9

The exact build options and source download are provided by
PrusaSlicer's deps/+FFmpeg/FFmpeg.cmake. This minimal static build disables GPL
and nonfree components and includes RTSP/HTTP networking, H.264/H.265/MJPEG video
decoding, local video capture (AVFoundation, DirectShow, or V4L2), and RGB scaling.
The bundled avfoundation-interrupt.patch adds startup cancellation to the pinned
AVFoundation backend; it is applied by the dependency build script. The complete application source and build scripts
are available in the accompanying PrusaSlicer fork.
