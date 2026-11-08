# Image Viewer next-version plan

Researched 2026-11-08 against commit `260b109`. Implemented for **0.3.0**.
See the [release QA report](qa-v0.3.md) for results and remaining native input
checks. The following records the original research and implementation plan.

**Findings**

| Feedback | Evidence | Work needed |
| --- | --- | --- |
| Installed app says 0.2.1 | Both the installed package and executable report 0.2.1. The checkout's `VERSION` and PKGBUILD say 0.2.2. The system desktop entry launches the packaged executable, and no override with the same desktop ID was found. | Install and verify the completed release through the desktop launch path. |
| Right-click anywhere on the picture | `ImageViewport.qml` already has a full-size MouseArea accepting right clicks. `Main.qml` opens the shared menu from its signal. One existing QML test clicks at a single position. | Reproduce the reported failure and extend coverage across the picture and overlapping items. Its precise cause remains unconfirmed. |
| Reuse a running instance | The desktop entry executes `omarchy-image-viewer -- %f`. Every GUI launch creates a new backend and QML window. There is no ownership or forwarding mechanism. | Forward subsequent launches to the existing GUI process. |
| Trackpad pinch | The viewport handles wheel angle deltas and left-button panning. It has no pinch handler. Installed Qt is 6.12. | Add native trackpad pinch through the existing zoom calculations. |

The menu label reads `Qt.application.version`, which is supplied from `VERSION`
at build time. The installed version observation is a deployment gap. The
previous cleanup built 0.2.2 for validation but did not install that package.

**1. Establish the reproduction baseline**

Use disposable synthetic portrait and landscape images. Compare the installed
0.2.1 behavior with a source build of the current 0.2.2 checkout on native
Wayland. Record the right-click failure position, menu visibility and position,
dock state, zoom state, and any QML warnings. Check whether an overlay consumes
the click or whether the menu opens outside the expected bounds.

Exercise the image center and edges, window corners, letterbox space, and the
transparent area around the collapsed and expanded dock. Repeat after zooming,
panning, rotation, and resizing. Distinguish ordinary mouse right-click from
the trackpad's secondary click. The existing offscreen test alone does not
establish that the reported native interaction works.

**2. Make context-menu handling consistent**

Keep one shared menu and one path for opening it with the correct coordinates
and invocation origin. Prefer the Qt Quick Controls `ContextMenu` attached
handler on the image surface, with forwarding from overlapping items only
where reproduction shows it is necessary. Qt supports platform context-menu
events on any Item, including right-click and the context-menu key.
[Qt ContextMenu documentation](https://doc.qt.io/qt-6/qml-qtquick-controls-contextmenu.html)

Separate secondary-click recognition from the MouseArea's left-button drag
behavior. Reset the dock-origin flag on each invocation. Map the requested
position explicitly to the menu's parent and keep the popup inside the window.
The dock's More button and Shift+F10 should open the same actions. Qt documents
both explicit parent-relative popup coordinates and window margins.
[Qt Menu documentation](https://doc.qt.io/qt-6/qml-qtquick-controls-menu.html)

Acceptance requires a right-click anywhere on the visible picture to open the
menu near the pointer, including areas beneath transparent overlays. Actual
controls must retain their left-click actions. Escape and outside click dismiss
the menu. Modal edit and help dialogs retain input ownership. Add focused QML
tests for the reproduced failure and edge positioning, alongside native QA.

Primary files are `src/ImageViewport.qml`, `src/Main.qml`, and
`tests/qml-tests.cpp`. Change `ViewerDock.qml` or `ViewerMenu.qml` only if the
reproduction requires it.

**3. Reuse the running GUI instance**

Use the session D-Bus, already a direct application dependency, for ownership
and launch forwarding. Keep the current desktop file identity and Exec route.
The newly launched process parses its arguments, completes standalone CLI modes,
and then either becomes the GUI owner or forwards an open/activate request and
exits. Ownership must be atomic, using non-queued, non-replacing service
registration. Qt exposes these ownership options directly.
[Qt D-Bus service registration](https://doc.qt.io/qt-6/qdbusconnectioninterface.html#registerService)

Export a small application-specific interface with an API version and an
acknowledgement. Register its receiving object before publishing ownership and
retain a bounded pending request while QML starts. A simultaneous cold launch
must produce one owner. A timeout while an owner remains registered must report
failure. If the owner disappears, retry ownership within a bounded deadline.
An unavailable session bus should produce a clear launch error.

Normalize a relative image path against the sending process's working directory
before forwarding. Accept a filesystem path or local file URL and preserve
spaces, Unicode, and leading dashes. Reject unsupported URL schemes and extra
positional arguments explicitly. Avoid filesystem probes in the forwarding
path that could block on a disconnected mount. Keep the one-image invocation
contract used by `%f`.

Send incoming images through a GUI open-request function that uses the existing
Save / Discard / Cancel flow. Directly calling `Backend::open()` would silently
ignore requests while a rotation or operation is pending. Keep one pending
external destination, coalescing rapid requests to the latest destination and
showing that destination in the edit prompt. Save failure retains the current
photo and edit. Cancel clears the pending external request. Once Save is chosen,
capture that continuation so a later request cannot change its destination.
Requests arriving during an operation wait in the bounded pending slot and are
re-evaluated after completion. Resolve an outstanding file picker before
accepting an external open so its late reply cannot replace the requested file.

Launching without an image should activate the existing window. Opening the
already displayed file should activate it while preserving its pending edit.
Invalid or unreadable input should show an error in that window while retaining
the last good frame. Closing and shutdown must reject new requests cleanly.

Restore a minimized window and request activation while preserving its other
window state. Forward a launcher's `XDG_ACTIVATION_TOKEN` when available and
consume it for the specific activation. Qt 6.12's Wayland implementation reads
this token during `requestActivate()`. Verify this behavior against the actual
file manager, including another workspace and a missing token. Compositor focus
policy remains an integration constraint.
[Qt 6.12 Wayland activation implementation](https://raw.githubusercontent.com/qt/qtbase/6.12/src/plugins/platforms/wayland/plugins/shellintegration/xdg-shell/qwaylandxdgshell.cpp)

Full desktop D-Bus activation would additionally require the standardized
application interface, a matching desktop filename, and an activation service.
The proposed launch forwarding can work through the current Exec entry. Use
the standard's activation metadata conventions for the forwarded token.
[Desktop Entry D-Bus activation specification](https://specifications.freedesktop.org/desktop-entry/latest/dbus.html)

Primary changes belong in `src/main.cpp`, a small instance-coordination class,
`Main.qml`, and the edit continuation state. Add the compiled class to qmake and
the test build. Add subprocess tests under the existing private D-Bus harness
for simultaneous startup, forwarding, owner exit, timeouts, and independent CLI
modes. Add QML tests for dirty edits, canceled/failed saves, operation deferral,
and picker races. Native acceptance requires one persistent viewer window after
repeated file-manager opens, with each accepted image appearing there.

**4. Add trackpad pinch zoom**

Add a `PinchHandler` to `ImageViewport.qml` with `target: null`. Feed its
multiplicative scale changes into `zoomAt()` around the gesture position in
viewport coordinates. This keeps zoom limits, pan clamping, DPI handling, and
debounced detail reads under the existing viewport logic. Qt documents this
manual scale integration.
[Qt PinchHandler documentation](https://doc.qt.io/qt-6/qml-qtquick-pinchhandler.html)

Use the native touchpad event route. Qt 6.12's handler explicitly processes
native begin, zoom, and end events. Automatic image rotation and translation
should be disabled for this interaction.
[Qt 6.12 PinchHandler implementation](https://raw.githubusercontent.com/qt/qtdeclarative/6.12/src/quick/handlers/qquickpinchhandler.cpp)

Enable pinch when a picture is actionable and a modal overlay is closed. End or
cancel a gesture safely if the photo changes, the window loses focus, or the
input device stops sending updates. Wheel zoom, keyboard zoom, and drag panning
must compose with successive pinches without jumping. Check that the same
physical gesture does not apply zoom twice.

Inject native gesture sequences in a focused QML test, including reversal,
repeated gestures, limits, non-central anchoring, and cancellation. A simulated
touchscreen gesture alone would miss the requested input path. Two-finger
pinch is delivered as native zoom on Wayland, whereas parallel two-finger
movement normally produces wheel events.
[Qt native gesture documentation](https://doc.qt.io/qt-6/qnativegestureevent.html)

Native acceptance requires smooth pinch in and out on the real trackpad, at
normal and scaled display settings, with the point under the gesture remaining
stable except when pan bounds require clamping. Inspect actual event delivery
if this fails before choosing a C++ fallback. Preserve the existing detail
memory limits and verify Fit still drops detail buffers.

**5. Package and verify the completed release**

Implement the context-menu correction, instance reuse, and pinch in separate
reviewable steps after the baseline reproduction. Update controls documentation,
architecture notes, and English/Swedish/Danish strings for new visible text.
Bump `VERSION` and PKGBUILD together to the agreed release version and regenerate
the deterministic source archive and pinned checksum.

Run the viewer build and test suite, a synthetic-image CLI inspection, and a
clean makepkg build including its checks. Also run the repository-required USB
tests, shell syntax checks, plugin validation, and `git diff --check`. Verify
`--inspect`, `--rotate`, `--formats`, help, and version remain independent of a
running GUI.

For delivery, install the resulting package, close the old viewer through its
normal edit-aware close flow, and relaunch from both the app launcher and file
manager. An already running older executable retains its old code after a
package update and has no new forwarding endpoint. Compare the package version,
installed binary version, and menu label after relaunch. Record native Wayland
results and application logs, and capture screenshots using synthetic content
for the changed interactions.

The outstanding research gates are the exact right-click failure, the file
manager's activation-token behavior, and real trackpad event delivery. The
implementation and acceptance checks above resolve each gate.
