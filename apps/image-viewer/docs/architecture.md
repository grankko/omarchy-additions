# Image Viewer architecture

Backend owns the visible photo and its source stamp on the GUI thread.
`m_path` identifies that source. `m_cursor` (previously `m_target`) tracks navigation independently.
`m_requested` is the latest pending read and `m_reading` is the in-flight read.
`m_serial` rejects superseded read results. `m_generation` invalidates cache
work. `m_check` carries the displayed stamp to background freshness validation.
`m_turns` counts unsaved quarter turns relative to the displayed source.
A displayed frame can outlive a failed request, including across folders.

| Transition | Contract |
| --- | --- |
| Open or navigate | Update the cursor, coalesce pending work, replace displayed identity only on success |
| Failed read | Keep the displayed frame and stamp, retain the failed cursor so a second arrow can pass it |
| Folder refresh | Publish a successful sorted snapshot for the current directory, retain that directory's snapshot on failure |
| Detail or cache check | Accept only results for the current generation, source, and relevant request |
| Save | Capture the displayed path, stamp and turns, validate and stage, check the source again, rename, verify the result |
| Copy or Send | Revalidate the displayed source, use full resolution for clipboard copy |
| Picker | Track the predicted or returned handle and one request identity. Open one valid local file. Cancel locally before waiting on the portal. |
| Shutdown | Never let a late worker call into a destroyed GUI object |

ImageIO decodes into implicitly shared QImages. The 48 MiB neighbor cache is a
retained-preview budget. The displayed image, decode working sets, renderer and
clipboard add their own memory. Active fit and detail are separate. Detail has a
64 MiB retained-output cap and 8192-pixel edge ceiling. Fit drops the detail
image and invalidates its jobs. Speculation skips unknown or expensive decodes
beyond a conservative 64 MiB preflight estimate, with a native JPEG downscaling
exception. These estimates do not bound a decoder's internal allocations.
Qt's process-wide image allocation policy stays in force.

There is one worker each for foreground, scan, background and user operations.
Foreground and scan coalesce to one pending request each. Freshness and detail
have priority over neighbor reads. Late results are invalidated before
publication. The pools drain after watchers during application teardown, outside
the GUI event loop. A kernel-blocked read can still hold its lane or delay
process exit. JPEG and PNG overwrite is qualified conservatively.
The original stays untouched until same-directory replacement. Preparation
includes source checks, staging/transforming, output verification and a staged
file sync. Cancellation and commit entry compete on one atomic phase transition.
Each JPEG helper has a 60-second monotonic deadline, bounded diagnostic capture,
and a terminate/kill grace period. Once commit starts, verify the destination
inode and sync the retained directory descriptor. A confirmed replacement clears
the edit even if directory sync warns. An unverified commit requires reload
before retry. The source stamp check is optimistic conflict detection across
clients.

Tests use temporary directories and synthetic images with distinct names, sizes
and colors. The Qt test executable separates image I/O, backend, integration,
and QML cases into source files. Replace external boundary behavior with controlled callbacks where
needed. Avoid sleeps for scheduling assertions. Use disposable fixture copies
for package and native QA. The [review handoff](image-viewer-review-plan.md)
records the original probes and remaining benchmark procedure.

The package is built from an allowlisted deterministic local archive generated
with `bin/source`. Its digest is pinned in the PKGBUILD and must be updated
after changes to packaged inputs. Package build and test output are separate
directories inside makepkg's staging area. `VERSION` supplies the application
version, and makepkg checks it against the package version.

`ImageViewport.qml` takes the backend and the device pixel ratio, and owns fit,
zoom, pan, and scene-graph presentation. `ViewerDock.qml` emits viewer actions
and a menu anchor. `UnsavedEdits.qml` receives the current capability and reason
and emits intent. The upper-right close button uses the window's normal close
request, including the existing edit prompt. Main owns continuation, so cancel or a failed save cannot
execute the queued navigation. Operation and status policy use enums, and
worker message keys translate through the current catalog on the GUI thread.

GUI launches claim one session D-Bus service through `Instance`. Secondary
processes forward one absolute local path and a launch activation token, then
exit after acknowledgement. CLI inspection, explicit rotation, decoder listing,
help, and version complete before claiming GUI ownership. Registration uses
non-queued, non-replacing ownership. A disappearing owner permits a bounded
retry. A registered owner that cannot acknowledge produces a launch error.
The API accepts only protocol version 1 and bounds paths and activation tokens.
Initial GUI launch, an explicit activation without a file, and restoring a
minimized window request activation. Forwarding an image to a visible window
preserves the caller's focus, avoiding focus-driven cursor warps while browsing
in a file manager. Activation tokens are consumed only when activation is
requested.

Main owns external-open continuation. It cancels an older picker before
requesting another image and keeps one pending external destination while an
operation runs. Dirty edits use the same Save / Discard / Cancel flow as
navigation. The destination used by an in-progress Save is captured separately
from later incoming requests. Cancel retains the displayed edit, and failed
Save cannot execute its continuation. Opening the displayed path only activates
the window unless a newer load must be superseded.

The image surface uses a ContextMenu handler above ordinary controls so
secondary clicks reach the shared menu without taking left-button drags or
button clicks. PinchHandler consumes native trackpad scale deltas with no target
transform. All zoom input runs through the viewport's zoomAt calculation,
including its pan bounds and delayed detail request. Pinch is disabled while
the window is inactive, the backend is busy, or a modal overlay is open.
