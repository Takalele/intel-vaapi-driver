# Intel VA-API driver for legacy hardware.

This is a fork with many changes, including but not limited to:

- Functioning Chromium support
- Fix JPEG decoding of oddly encoded files on gen7/8 (https://github.com/intel/intel-vaapi-driver/pull/514)
- fix exporting buffers with 3 planes and `VA_EXPORT_SURFACE_SEPARATE_LAYERS` (https://github.com/intel/intel-vaapi-driver/pull/530)
- `i965_pci_ids`: Add CFL PCI ID found on Xeon W-1290P (https://github.com/intel/intel-vaapi-driver/pull/548)
- Make wl_drm optional (https://github.com/intel/intel-vaapi-driver/pull/566)
- Expose `VAConfigAttribMaxPictureWidth` and `VAConfigAttribMaxPictureHeight`.
- Full support for `vaSyncBuffer`, `vaSyncSurface2` and `vaMapBuffer2`. (enabling `async_depth` in FFMPEG)
- More accurate per-codec limits.
- Improved hybrid codec driver support.
- Expose ARGB format support. (https://github.com/intel/intel-vaapi-driver/issues/500)

The `NEWS` file contains a more detailed changelog.

# Release schedule

This driver has a single digit count of planned releases, after that this driver will too be strictly
in maintenance mode, since it's basically feature complete.

Until maintenance mode is reached, the driver will continue to increment on the `2.4.x` version branch,
and then bump the version to `2.5.x` where only fixes for regressions and critical issues will be accepted.

# Reporting issues

- Reproducers are highly welcome (i don't like seeing "it doesn't work")
- Sample footage (if possible) for any fork regressions are welcome
- Remember that I am just some random person on the Internet.

# GPU support matrix

| GPU Generation | MPEG-2              | VC1    | H.264 (AVC)         | H.265 (HEVC)        | JPEG                | VP8                 | VP9                    |
|----------------|---------------------|--------|---------------------|---------------------|---------------------|---------------------|------------------------|
| G45 (CTG)      | DECODE              |        |                     |                     |                     |                     |                        |
| ILK (5.x)      | DECODE              |        | DECODE              |                     |                     |                     |                        |
| SNB (6.x)      | DECODE              | DECODE | DECODE<br>ENCODE[1] |                     |                     |                     |                        |
| IVB (7.0)      | DECODE<br>ENCODE[1] | DECODE | DECODE<br>ENCODE[1] |                     | DECODE              |                     |                        |
| HSW (7.5)      | DECODE<br>ENCODE[1] | DECODE | DECODE<br>ENCODE[1] |                     | DECODE              | DECODE              | DECODE[2]              |
| CHV (8.0 LP)   | DECODE<br>ENCODE[1] | DECODE | DECODE<br>ENCODE[1] | DECODE              | DECODE<br>ENCODE[1] | DECODE<br>ENCODE[1] | DECODE[2]              |
| BDW (8.0) [3]  | DECODE<br>ENCODE[1] | DECODE | DECODE<br>ENCODE[1] |                     | DECODE              | DECODE              | DECODE[2]              |
| SKL (9.0) [3]  | DECODE<br>ENCODE[1] | DECODE | DECODE<br>ENCODE[4] | DECODE<br>ENCODE[1] | DECODE<br>ENCODE[1] | DECODE<br>ENCODE[1] | DECODE[2]              |
| KBL (9.5) [3]  | DECODE<br>ENCODE[1] | DECODE | DECODE<br>ENCODE[4] | DECODE<br>ENCODE[1] | DECODE<br>ENCODE[1] | DECODE<br>ENCODE[1] | DECODE[5]<br>ENCODE[6] |
| ??? (10.0)     | ?                   | ?      | ?                   | ?                   | ?                   | ?                   | ?                      |

# Workaround for the Chromium dmabuf export bug (issue #564)

Brave and other Chromium-based browsers fall back to software decode on
Intel Gen 6-9 (i965) graphics. The browser log shows `vaapi video
decoder.cc: failed initializing the frame pool`, and the VCS (video)
engine stays idle at about 0% while a video plays.

Root cause. The i965 driver allocates a decode surface's GEM buffer
lazily, at its first render (`i965_hw_putimage`). Chromium's dmabuf
frame pool exports the surface to a dmabuf *before* any frame is
rendered into it. `i965_ExportSurfaceHandle` then hits the
`!obj_surface->bo` guard and returns `VA_STATUS_ERROR_INVALID_SURFACE`,
so the frame pool cannot initialize and the decoder falls back to
software. This is [issue #564](https://github.com/intel/intel-vaapi-driver/issues/564).
The project is discontinued, so the fix does not land upstream.

The fix. This branch patches `i965_ExportSurfaceHandle` to allocate the
GEM buffer on demand when it is still missing. The fourcc is derived
from the runtime format set at `CreateSurfaces` time (`NV12` for
YUV420, `P010` for YUV420_10). The first real render finds the buffer
and the matching format already set and reuses them.

## The deployed driver

The snap environments load a VA-API 1.20 driver, so the `.so` must
export `__vaDriverInit_1_20` and be linked against a glibc no newer than
the snap's. The driver deployed as the workaround is a patched
i965-va-driver 2.4.1 build that exports `__vaDriverInit_1_20`
(reference md5 `925136594899`). A stock build on a modern host targets
the host libva (VA-API 1.23) and exports `__vaDriverInit_1_23`, which
will not load in the snap.

## Install

Snap packages are read-only squashfs, so the bundled driver cannot be
edited in place. Setting `LIBVA_DRIVERS_PATH` for the browser's GPU
process also does not work (Chromium strips it from child processes).
The working approach is a bind mount of the patched `.so` over the
snap's driver file, run at boot by a systemd unit.

Each browser snap bundles its own driver and loads it from a different
path:

| Browser | content snap | driver path (revision is `*`) |
|---|---|---|
| Brave | gnome-42-2204 | `/snap/gnome-42-2204/*/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so` |
| Firefox | mesa-2404 (`gpu-2404`) | `/snap/mesa-2404/*/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so` |

Put the patched driver at a fixed path and bind it over every matching
snap file. Save the script below as `install-workaround.sh` and run it
(it needs sudo and is idempotent).

```sh
#!/bin/sh
# Bind-mount a patched i965 VA-API driver over the snap-bundled one,
# for Brave (gnome-*) and Firefox (mesa-2404). Idempotent.
set -eu

PATCHED="$HOME/lib/dri/i965_drv_video.so"
[ -f "$PATCHED" ] || { echo "patched driver not found at $PATCHED"; exit 1; }
chmod 644 "$PATCHED"

# Mount script (installed system-wide, runs at boot)
sudo tee /usr/local/sbin/i965-patch-mount.sh >/dev/null <<SH
#!/bin/sh
PATCHED=$PATCHED
[ -f "\$PATCHED" ] || exit 0
for t in /snap/gnome-42-2204/*/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so \\
         /snap/gnome-46-2404/*/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so \\
         /snap/gnome-44-2204/*/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so \\
         /snap/mesa-2404/*/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so; do
  [ -f "\$t" ] || continue
  mountpoint -q "\$t" && continue
  mount --bind "\$PATCHED" "\$t" 2>/dev/null && echo "mounted: \$t"
done
exit 0
SH
sudo chmod 755 /usr/local/sbin/i965-patch-mount.sh

# systemd unit (mounts at every boot)
sudo tee /etc/systemd/system/h264-i965.service >/dev/null <<'UNIT'
[Unit]
Description=Patched i965 VA-API driver for snap browsers (H.264 hardware decode)
After=local-fs.target

[Service]
Type=oneshot
RemainAfterExit=yes
ExecStart=/usr/local/sbin/i965-patch-mount.sh

[Install]
WantedBy=multi-user.target
UNIT
sudo systemctl daemon-reload
sudo systemctl enable --now h264-i965.service

echo "Installed. Restart the browser completely (quit, including tray, then start)."
echo "Verify: play an H.264 video, then run: sudo intel_gpu_top  (VCS column > 0%)"
```

After a snap update a new revision appears and the glob covers it. Re-run
`sudo /usr/local/sbin/i965-patch-mount.sh` and restart the browser.

## Run

Quit the browser completely, including the tray icon, and start it
again. Running instances keep the old driver in memory.

## Verify

Play any H.264 video and, in a terminal:

```
sudo intel_gpu_top
```

The VCS column (video engine) must be above 0% while the video plays
and about 0% when idle. That is the hardware decoder active. On this
machine the Brave path was measured at roughly 15% VCS during playback.

To confirm the running process loaded the patched file (Brave example):

```
GPU_PID=$(pgrep -f 'brave.*--type=gpu-process' | head -1)
sudo nsenter -t "$GPU_PID" -m md5sum \
  /snap/gnome-42-2204/*/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so
# expect 925136594899...
```

The same check works for Firefox. The decode process is a content or
media process, and `/proc/PID/maps` shows the driver it loaded:

```
for p in $(pgrep -x firefox); do
  grep -H i965_drv_video /proc/$p/maps 2>/dev/null
done
# expect: /snap/mesa-2404/*/usr/lib/x86_64-linux-gnu/dri/i965_drv_video.so
```

## Revert

```
sudo systemctl disable --now h264-i965
sudo umount $(mount | grep i965_drv_video | awk '{print $3}')
```

## Rebuild the driver from source

The snaps bundle i965-va-driver 2.4.1. This repo is a newer 2.4.x
series, so a drop-in for the snap should be built from a version whose
libva and glibc symbols the snap accepts (2.4.1 with this patch is the
reference build). Meson derives the driver init symbol from the host
libva, so a stock build on a current host exports
`__vaDriverInit_1_23`. To target the snap's VA-API 1.20, pin the minor
version in `meson.build`, after the pkgconfig lookup:

```
  va_api_minor_version = '20'
```

Check the symbol before deploying:

```
nm -D i965_drv_video.so | grep __vaDriverInit
# expect: __vaDriverInit_1_20
```

[1] - Intel's hardware encoder uses GPU resources during an encode, high GPU load can cause bad encoding performance.

[2] - "Hybrid" support only, known to be buggy.

[3] - Supported by the iHD driver, **you shouldn't be using this driver**

[4] - Supports Low Power encoders, which fully offload the work without using GPU resources.

[5] - Supports VP9 natively.

[6] - Encoding is supported by this driver, known to be buggy. Likely safe up to 1920x1080 at 60 FPS.

# License.

```
MIT License

Copyright (c) 2023 Intel Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```