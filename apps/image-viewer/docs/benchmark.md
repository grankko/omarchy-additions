# Viewer benchmark procedure

Use generated or disposable images in a temporary directory. Never clear the
machine's filesystem cache. Record the viewer build, Qt version, graphics
backend, viewport and device pixel ratio, fixture dimensions and depths, and
whether the directory is local or mounted. Report the number of observations,
warmup, median and p95. A fresh viewer process starts with a cold preview cache
even when Linux already has the source data cached.

Run with `IMAGE_VIEWER_TRACE=1 QT_FORCE_STDERR_LOGGING=1` and capture stderr.
Run again with `QSG_RHI_BACKEND=opengl QSG_INFO=1` to request RHI OpenGL. Confirm
which renderer the process actually uses. Keep fixture and viewport constant.
Open one image and wait for the initial presentation. Warm the two neighbors by
navigating back and forth. Alternate directions for at least 30 settled
navigation actions, with no changes to the directory. Repeat a rapid series
separately. Include transparent PNG, EXIF JPEG, zoom and Fit cycles, and a
directory with many entries.

Trace records are `viewer-trace NANOSECONDS EVENT REQUEST VALUE`. The clock is
monotonic and starts at the first trace event. `request` through
`frame-submitted` measures from backend request to Qt Quick swap submission.
It does not measure physical scanout or keyboard delivery. Report cache hits
separately from misses. The value for `frame-installed` is retained fit bytes.
`cache-bytes` reports neighbor cache cost. `detail-start` gives the requested
edge and `detail-drop` confirms release by the backend. Save/copy and graphics
allocations can still raise total RSS.

Use `python3 tests/report-trace.py TRACE` from the app directory. For a
repeatable settled loop, create two disposable JPEGs named `Viewer bench 1.jpg`
and `Viewer bench 2.jpg`, then run:

```sh
VIEWER_BENCH_DIR=/path/to/fixtures VIEWER_TEST_PLATFORM=wayland \
IMAGE_VIEWER_TRACE=1 QT_QUICK_BACKEND=software \
./bin/test benchmarkNavigation -o /path/to/trace,txt
```

Replace the software override with `QSG_RHI_BACKEND=opengl` for the RHI run. For native
sampling, read `VmHWM` and `VmRSS` in the viewer process's `/proc/PID/status`,
PSS in `smaps_rollup`, CPU ticks in `stat`, and read totals in `io`. Record
before/after during navigation and pan/zoom, alongside idle and detail/Fit
observations. On Linux, filesystem cache, graphics resources and allocator
retention are distinct from live QImage buffers. For mounted tests, identify
the mount type and injected conditions, and use authorized disposable data.
A delayed FUSE fixture is not evidence about the behavior of a real SMB server.

Compare against a like-for-like build or state which baseline signals were
unavailable. Review the warm target of 100 ms through frame submission and flag
differences beyond 10% under equivalent conditions. Avoid inferring an idle
wake-up rate from a point-in-time RSS measurement.
