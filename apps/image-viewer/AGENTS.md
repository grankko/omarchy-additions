# Image Viewer

This is a standalone C++20 and Qt Quick app. The repository's USB and system
configuration safeguards still apply. Never edit /usr/share/omarchy or use real
photos, mounted drives, desktop settings, or the live clipboard as automated
fixtures. Keep machine paths, credentials, device identifiers, binaries, and
generated logs out of source.

From this directory, run `./bin/build`, `./bin/test`,
`./build/omarchy-image-viewer --inspect --json -- FIXTURE`, and
`./bin/source && (cd pkgbuild && makepkg -C)`. `--rotate` overwrites its argument. Use a disposable
fixture. For native QA, run the built or installed binary with a disposable
fixture under Wayland. Check keyboard, dock, menu, picker, edit recovery, and
logs on a small or scaled viewport. Capture synthetic content for UI reports.
From the repository root, run `node --test tests/*.test.cjs`, `bash -n` on each
USB Storage and Image Viewer shell script,
`omarchy plugin validate plugins/usb-storage`, and `git diff --check` before
delivery.

Backend owns GUI-thread state and jobs. ImageIO owns format qualification,
decoding and overwrite. PhotoItem owns scene-graph resources. Main.qml owns
interactions. Theme and Translator provide desktop integration. Update qmake
lists and the resource manifest when adding compiled sources or QML.

Read [the architecture contract](docs/architecture.md) before changing these
boundaries. Keep displayed identity, cursor, request, and job identity separate.
Workers take values and publish values. Late work cannot change a newer photo,
edit, or operation. Keep the last good frame after a failed read and the pending
rotation after an unsuccessful save. Verify the original before overwrite and
never retry an uncertain commit automatically. Use the architecture contract for
fixture and memory policy and update it with public behavior and catalog changes.
