# Image Viewer 0.3 QA

Validated on Omarchy with Qt 6.12.0 on 2026-11-08.

The release adds session D-Bus launch forwarding, native trackpad pinch zoom,
and a platform context-menu handler over the picture surface. Menu entry
points share explicit window bounds. Opening a new image preserves the existing
Save / Discard / Cancel policy and shows the pending destination in the prompt.

**Automated checks**

- Viewer suite reports 31 passes and one expected skip for the opt-in navigation
  benchmark. Added checks cover launch forwarding, simultaneous ownership,
  owner exit, native pinch sequences, disabled pinch, repeated zoom gestures,
  external-open edit cancellation, verified save, and save conflict.
- Three focused native Wayland cases pass for menu positions, pinch events,
  and external-open edit protection. English and Danish menu screenshots were
  rendered with synthetic images and a temporary language file.
- The USB suite reports 22 passes. Shell syntax, USB plugin validation, and
  Git whitespace checks pass.
- The release binary reports 0.3.0 and successfully inspects and rotates a
  disposable Qt-generated PNG through the CLI.

**Native QA boundaries**

The automated native window could remain inactive under compositor focus
policy. Menu tests verify Escape when active and close the popup explicitly
when inactive. A first attempt to send Escape to an inactive native window
failed. That result was a focus-dependent test limitation.

The original right-click failure was not reproduced by the synthetic click
matrix against the older QML. The new handler uses Qt's platform context-menu
events instead of waiting for a MouseArea click. Screenshot review also caught
an opening menu extending beyond the right edge, which motivated explicit
position bounds and checks of the settled popup background.

Pinch QA injects native touchpad gesture events through a real Wayland window.
Physical trackpad input and focus transfer between workspaces require an
interactive check on the target desktop. Application activation forwards the
launcher's token when available and follows compositor focus policy.

All images used for these checks are synthetic. CLI overwrite checks use a
disposable copy. The UI checks use isolated D-Bus sessions, temporary theme and
language fixtures, and application-local logs.
