# Omarchy image viewer implementation plan

Status **Implemented**. See the [viewer README](../apps/image-viewer/README.md) and [v0.2 QA report](../apps/image-viewer/docs/qa-v0.2.md) for the current behavior and limits.

Build a standalone image viewer for Omarchy that makes the most of a small screen and stays responsive when browsing photos on mounted network shares. Use the bottom hover dock and right-click menu from the selected mockups.

## Layout and interaction

- The photo gets the full viewport. Open in fit-to-window mode, preserve aspect ratio, and offer actual-size viewing alongside Omarchy’s fullscreen shortcut. Keep the window free of permanent toolbars, sidebars, and thumbnail strips.
- Leave a small handle at the bottom center. Hovering, clicking, or focusing it opens a compact translucent dock over the photo. Keep the dock open while the pointer or keyboard focus is inside it. Use the Omarchy accent and a dark scrim that keeps the controls legible over light and dark photos.
- Order the dock as previous, next, zoom out, zoom in, fit, rotate, **Save**, and more actions. Left and Right move between files. Scroll zooms around the pointer, and dragging pans when zoomed. Reset to fit on each new photo. Use natural filename order within the current folder, with no wraparound in the first version.
- Right-click opens a menu at the pointer with fit, rotation, **Save**, **Copy image**, and **Send to → LocalSend**. Copy puts the full-resolution image with the current rotation on the clipboard. LocalSend receives the current file on disk and opens its recipient picker. If there is an unsaved rotation, offer to save before handing off or to send the saved version.
- A photo change keeps the previous frame visible until the requested frame is ready, then swaps directly. Use a delayed loading indicator for slow reads and identify failures without clearing the last good image. Disable actions on the photo while a different file is pending so they always apply to the intended file.
- **Save** and Ctrl+S are enabled only when a supported, writable image has a net rotation relative to its saved orientation. Four quarter turns return to the clean state. Keep the edit pending until Save succeeds or the user discards it. Prompt before leaving or closing an edited photo, with **Save**, **Discard**, and **Cancel** as applicable.

## Network folders and cache

Use the mounted filesystem paths from the file manager or file picker. Apply the same bounded policy to local and mounted folders so browsing behavior does not depend on mount detection.

1. Decode the opened photo immediately. Enumerate and sort its siblings in the background, using the supported suffixes for the initial list. Probe or decode only files requested for viewing or prefetch. Refresh the folder list on demand and after returning to a stale folder or encountering a missing entry. Avoid directory scans and per-file probes on each arrow press.
2. Render a viewport-sized preview at the current display scale where the decoder can scale efficiently. Upgrade the active image when zoom needs more detail. Decode full resolution for copy or save and release those working buffers after the operation, apart from any image retained to serve the clipboard.
3. After the foreground image is ready, prefetch one neighbor in the direction of travel, then one on the other side if there is room and the first prefetch finishes. Start with one speculative read at a time and a **48 MiB** byte budget for cached neighbor previews. Evict least recently used entries and release the cache on folder changes. Track the displayed image, texture uploads, and in-flight decoder memory separately when measuring total footprint.
4. Give navigation priority over speculative work. Coalesce rapid key presses to the latest target and discard superseded results. Keep foreground work separate from the prefetch worker so a blocked read-ahead does not occupy the foreground slot. Bound workers and queues even if a mount stalls. A blocked filesystem call can outlive a cancelled request, so cancellation invalidates its result and the UI remains usable while the worker unwinds.
5. Serve recent warm previews immediately and check freshness asynchronously. Key entries by path and preview size, with file size, modification time, and file identity where the mount exposes it. Invalidate on a detected change, successful save, folder refresh, or mount error. Recheck the source for full-resolution actions and before saving. Freshness checks are limited by the mount's metadata caching.

The first version keeps previews in RAM for the browsing session. Measure cold reads and repeat visits before considering an opt-in disk cache.

## Formats and overwrite behavior

- Target JPEG, PNG, WebP, TIFF, GIF, BMP, HEIC/HEIF, and AVIF for viewing. Verify codecs on Omarchy with sample files. Qt's standard plugins cover the common baseline, and Arch's `kimageformats` provides HEIC/HEIF and AVIF plugins. Define first-frame viewing for animated or multi-page files in the initial release and keep those files read-only in the viewer. [Qt image formats](https://doc.qt.io/qt-6.8/qtimageformats-index.html) · [Arch plugin list](https://archlinux.org/packages/extra/x86_64/kimageformats/files/)
- Qualify overwrite separately per format. Start with JPEG and PNG and add other formats after round-trip tests. Respect EXIF orientation on load, and verify color profiles, transparency, and relevant metadata on save. Use an exact lossless pixel rotation for ordinary JPEGs where supported. Use the orientation tag for standard EXIF JPEGs and images that would otherwise require cropping.
- **Save** replaces the opened file at its existing path in the same format. Encode to a temporary sibling and commit with a filesystem replace operation. Require a supported writer and a path that can be safely replaced. Treat symlink or hard-linked files as view-only in the first version. Check for detected external edits just before commit and offer to reload on a conflict. This check is best effort across clients on a shared mount. [Linux rename semantics](https://man7.org/linux/man-pages/man2/rename.2.html)
- Keep the pending edit on a definite save failure. If a network interruption makes the commit outcome unclear, report that state and verify the file before offering a retry. A mount must support same-directory replacement to enable saving. [NFS rename failure semantics](https://man7.org/linux/man-pages/man2/rename.2.html#BUGS)
- Verify LocalSend with absolute filesystem paths, including spaces and Unicode, on local and mounted folders. Test both an already-running instance and a fresh launch. LocalSend owns recipient selection and transfer. [LocalSend Linux handoff issue](https://github.com/localsend/localsend/issues/3417)

## Implementation sequence

1. **Viewer and interaction.** Add a self-contained Qt Quick app under `apps/image-viewer/` with a qmake6 build and a small C++ backend for file I/O, decode, clipboard, and application handoff. Keep UI in QML. Support opening from the file manager, a portal file picker, and a path argument. Implement the selected layout and navigation with off-thread decode, then record a baseline before adding read-ahead.
2. **Network browsing.** Add the folder snapshot, bounded preview cache, prioritized loading, and delayed loading and error states. Tune on representative mounted folders and check that read-ahead helps forward and backward browsing without noticeably increasing memory or network traffic.
3. **Save, copy, and sharing.** Implement the rotation state and overwrite checks with disposable image fixtures. Verify image and metadata round trips, clipboard pasting into another app, and LocalSend staging.
4. **Package and verify.** Give the viewer a separate Arch package, launcher entry, and icon. Leave file associations under user control. Run the viewer checks, inspect the installed UI and logs, capture screenshots, and run the repository's Node, Bash syntax, and USB plugin validation checks.

The viewer is a separate desktop process with its own install and uninstall path. The existing repository scripts continue to manage the USB Storage shell plugin.

## Acceptance checks

- On a 1366×768 viewport or smaller, the collapsed handle is the only persistent on-image control. Test dock reveal, keyboard focus, right-click, fullscreen, and zoom over light and dark photos.
- Compare cold first paint, warm next/previous latency, and peak memory with Loupe on the same machine and photo set. Aim for a warm switch under **100 ms** and confirm the preview cache stays within its budget during rapid browsing. Include local storage and the user's mounted share type, with read latency and bytes read recorded for repeatable comparisons.
- Exercise corrupt and large files, long directories, rapid direction changes, stale previews, disconnects, and permission failures. Verify older decode results cannot replace the requested photo, and that Save remains tied to the correct file through failures and retries.
- Verify rotation and overwrite on disposable files for each writable format and mount type. Include external edits, a read-only share, failed replacement, and an interrupted connection.
