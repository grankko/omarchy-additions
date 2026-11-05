# Image Viewer v0.2 QA

## Handoff findings

| Findings | Changes and evidence |
| --- | --- |
| F1 | The live catalog is synchronized and the change signal is outside the lock. Tested concurrent lookup, atomic language changes, malformed catalogs, and missing keys. |
| F2–F3 | JPEG helpers have a monotonic deadline and can be cancelled before commit. Tested timeout against a helper that ignores terminate, explicit cancellation, and rejection of truncated output. Picker tests use a private D-Bus with delayed replies, an alternate handle, service loss, malformed results, and early success. Native portal cancellation also recovered twice. |
| F4, F11 | Navigation advances past failed entries while the last image stays visible. Indexed snapshots preserve natural order and retain data on failed refresh. Tested consecutive bad entries, reversal, deletion, refresh failure, and request coalescing. |
| F5–F6 | Dedicated bounded worker lanes isolate the foreground from scan and detail work. Cooperative stop checks avoid later decode or resize work on superseded requests where possible. Fit releases the detail buffer. Tests hold a detail read and an old folder scan while foreground navigation completes. Fit returns to the retained preview. A subprocess probe closed its window while a worker was held and exited after that worker returned. |
| F7 | Scene-graph failure removes stale pixels and disables photo actions until recovery. Offscreen tests inject node and texture failures and verify recovery to a fresh image. The QML also ran under software and RHI on Wayland. |
| F8 | Overwrite uses an anchored directory descriptor and verifies the replacement. Staged-file sync, rename refusal, success despite a rename error, directory-sync warnings, and uncertain verification are distinct. Fixtures exercise APNG, unsupported PNG and JPEG markers, indexed and high-depth PNG, and all eight EXIF starting orientations with all three supported rotations. A lossless pixel rotation swaps asymmetric JFIF densities and preserves the ICC profile and comment. |
| F9 | Status policy no longer compares translated text. Workers return source messages and the GUI translates them from the active catalog. Viewport, dock, and edit prompt are separate. QML tests verify cancel, verified Save and Continue, and save failure without continuing. |
| F10 | Version comes from `VERSION` and makepkg checks it. Repeated source archive generation matched the pinned digest. Fresh standalone staging built and tested the package without an adjacent checkout. |
| F12 | Timings include Qt Quick frame submission. The software default met the latency target with lower peak RSS than the OpenGL run, and renderer overrides remain available. Theme updates suppress unchanged notifications. |

The clean Arch package run on Qt 6.12.0 reported 27 passes and one expected
skip for the separately invoked benchmark. The USB suite reported 22 passes.
Bash syntax, the desktop entry, USB plugin validation, and Git whitespace checks
passed. Native QA also pasted a full-resolution rotated PNG into an independent
Wayland client on a private compositor. A targeted ThreadSanitizer probe of the
production translator passed with uninstrumented modules and framework thread
leaks filtered. The unfiltered probe reported activity inside system Qt D-Bus
libraries. This does not establish a sanitizer-clean Qt build.

## Reference measurements

Two disposable 3000×2000 JPEGs, one cold preview-cache request and 34 settled
warm switches per mode. Timings run from request to Qt Quick swap submission.
They exclude keyboard dispatch and physical scanout.

| Wayland run | Cold request, one sample | Warm median | Warm p95 | Peak RSS |
| --- | ---: | ---: | ---: | ---: |
| Software, 630×674 logical at DPR 2 | 572 ms | 3.25 ms | 3.90 ms | 122 MiB |
| OpenGL RHI, same viewport | 336 ms | 1.71 ms | 5.42 ms | 167 MiB |
| Software, delayed disposable FUSE mount | 2,193 ms | 4.02 ms | 5.04 ms | Unmeasured |

The delayed mount injected 35 ms per read and logged 116 reads totaling about
1.75 MiB across the run. The native local runs each retained at most 6 MiB
in the measured neighbor cache. The v0.1 handoff measured a 15,360,000-byte
2400×1600 detail frame remaining after reduced demand. The v0.2 regression
returns to the 512-edge fit image after dropping detail. The old backend trace
did not measure presentation, so the earlier latency numbers do not form a
like-for-like release comparison.

A 36-step synthetic pan/zoom sequence used 334 ms of viewer CPU with software
rendering and 39 ms with OpenGL. The corresponding wall times were 1,050 ms and
943 ms. Each 16-second idle sample used under 3 ms of viewer CPU and had about
60 voluntary context switches, a scheduler proxy for idle activity. Neither
sample emitted a theme change. One separate offscreen probe enumerated 100
decoder suffixes in 54 ms and sorted 10,000 directory entries in 65 ms.

The mounted probe used synthetic FUSE delays. Real SMB and NFS were outside
the measured scope. Cold timings are single observations with the OS file
cache left alone. A worker blocked in a kernel filesystem call can delay its
lane and process exit. Overwrite conflict detection is optimistic across
clients.
