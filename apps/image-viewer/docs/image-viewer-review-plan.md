# Image viewer review and implementation handoff

Reviewed: 2026-11-05. Status at review: **plan only**.
See the [v0.2 implementation and QA report](qa-v0.2.md)
for follow-up evidence.

Audience: an implementation agent working at a lower reasoning budget. Execute
the numbered work packages below in order, keeping each change independently
reviewable. This document supplements the original
[product plan](image-viewer-plan.md); it does not replace its interaction design.

## Scope and evidence

The review covers all viewer C++, QML, translations, tests, build scripts,
packaging, and documentation under `apps/image-viewer/`. Line numbers below refer
to the reviewed working tree and are navigation aids; use the named symbol if
they move. The Git base was `b021889`, but the viewer was **untracked**, and the
root README already had local modifications. Do not reset, clean, or replace
that work. This plan is the only repository file added by the review.

Baseline checks completed:

| Check | Result |
| --- | --- |
| `./apps/image-viewer/bin/build` | Passed |
| `./apps/image-viewer/bin/test` | 11 QtTest passes, including initialization/cleanup; 9 test functions |
| `node --test tests/*.test.cjs` | 22 passed |
| `bash -n` on install/uninstall, viewer scripts, PKGBUILD, USB helper | Passed |
| `omarchy plugin validate plugins/usb-storage` | Exit 0 |

The installed Qt version was 6.12.0, on x86_64. Viewer tests use the offscreen
platform and software scene graph. They do **not** establish native Wayland,
hardware-renderer, real portal, mounted-share, or installed-package correctness.
No existing images were overwritten, no mount was disconnected, and no package
was installed or removed during this review.

Additional disposable probes compiled against the actual backend/image I/O:

* With `photo1.png` valid, `photo2.png` corrupt, and `photo3.png` valid, pressing
  Next, waiting for the error, and pressing Next again kept showing `photo1.png`
  and retried `photo2.png`. This confirms the navigation trap, not just a possible
  race.
* A 2400×1600 image initially decoded at edge 512 occupied 698,368 image bytes.
  After requesting detail 2400 and then detail 512, the retained frame was still
  2400×1600, or 15,360,000 bytes. The QML Fit action does even less: it changes
  geometry without requesting a smaller frame.
* With the global Qt pool limited to one thread and occupied by a controlled
  semaphore task, a new foreground open remained pending. Independent watchers
  do not provide independent execution capacity.
* Single-process CLI measurements using Linux `wait4` peak RSS: `--formats`
  peaked at 71,604 KiB; `--inspect` of a synthetic 6000×4000 RGBA PNG peaked at
  244,476 KiB even though inspection requests a 512-edge preview. The compressed
  PNG was only 101,467 bytes. These are single samples, not comparative UI
  benchmarks or a leak measurement. They demonstrate why cache bytes alone do
  not describe memory use. Temporary probes were outside the repository.

## Findings and priorities

P1 means correctness or loss of normal operation under a supported scenario;
P2 means important robustness, resource use, or maintainability; P3 means
optimization that needs measurements before changing policy.

| ID | Priority | Finding and evidence | Work package |
| --- | --- | --- | --- |
| F1 | P1 | Custom translator reads and replaces the same `QJsonObject` without synchronization while worker threads call `tr()`/`translate()`. Confirmed unsafe access pattern; no crash was induced. | 2 |
| F2 | P1 | Both JPEG helpers use `waitForFinished(-1)`. A stalled helper leaves `m_operating` true, and QML rejects close/open/navigation. Source-confirmed hang path. | 4 |
| F3 | P1 | An accepted portal request has no service-loss recovery or application-side cancel/close path. If no Response arrives, `operating()` remains true and ordinary closing is refused. Source-confirmed recovery gap. | 5 |
| F4 | P2 | A corrupt middle sibling traps sequential navigation after each failed request. Reproduced. | 3 |
| F5 | P2 | Full decode/transformation memory is separate from the 48 MiB cache; detail frames never shrink; speculative decode can overlap foreground/full-resolution operations. Partly measured, partly source-confirmed. | 7 |
| F6 | P2 | All jobs use the global pool; foreground navigation shares its single logical loader with detail reads. Result invalidation is present but does not interrupt blocked work. Source-confirmed and pool contention reproduced. | 6 |
| F7 | P2 | Rendering allocation failure can return a newly created image node without a texture, contrary to Qt's contract; replacement failure can leave old pixels with new backend identity. Conditional failure path, not reproduced on the real renderer. | 8 |
| F8 | P2 | Save has substantial safeguards, but fault-path coverage is narrow and parent-directory durability is not synchronized after rename. No existing data-loss incident is claimed. | 4, 9 |
| F9 | P2 | Status severity, operation kind, save outcome, and recovery are encoded in strings/booleans; QML decides timeout from translated message text. Tests omit the production translator and most asynchronous/error transitions. | 1, 2, 10 |
| F10 | P2 | No app-local agent instructions or architecture contract; original plan still reads like future implementation. Rotated build logs are unignored; packaging depends on an adjacent working tree instead of declared source. | 1, 11 |
| F11 | P2 | Folder scan returns only a list, so inaccessible and empty directories are indistinguishable; repeated `indexOf` calls make navigation/state queries linear in folder size. | 3, 6 |
| F12 | P3 | Software rendering is forced by default without recorded comparative evidence. Trace logging measures backend readiness, not first visible presentation. | 12 |

Preserve the good parts: off-thread filesystem work, request serials and cache
generations, keeping the last good frame on decode failure, bounded cache cost,
first-frame viewing, conservative write qualification, source stamp checks,
sibling temporary files, and explicit uncertain-save handling. This is a
targeted hardening project, not a framework or UI rewrite.

## Invariants for every work package

1. Backend/UI state changes occur on the GUI thread. Workers receive immutable
   job values and return values. They must not capture a live `Backend*`.
2. Distinguish the displayed file, navigation cursor, requested file, and
   in-flight job. Save/copy/send always bind to the displayed file and its stamp,
   never merely the latest navigation cursor.
3. Late results cannot replace a newer photo, clear a newer error, change a
   newer edit, or complete a newer portal request. Every job has sufficient
   request/generation identity to reject it.
4. A failed read keeps the last good image; a definite failed save keeps the
   pending rotation. An uncertain commit blocks another save until verified
   reload. Never automatically repeat a rotation after a possibly committed
   rename.
5. Full-resolution copy/save always re-read/revalidate the source. Preview
   pixels, including sRGB-converted display pixels, are never an overwrite
   source. Clipboard ownership may legitimately retain a full-resolution image.
6. QImage copies are implicitly shared. Count unique retained buffers where
   possible; do not add deep copies just to make ownership look explicit.
7. File/codec work and application-wide pool draining must not run in a GUI
   callback. Logical cancellation invalidates a result; it cannot cancel an
   arbitrary blocked filesystem syscall.
8. Never broaden JPEG/PNG write eligibility to get a test passing. Do not add
   force-overwrite, format fallback, lossy JPEG re-encoding, or direct truncation
   of the original. Keep symlink/hardlink/ownership/xattr restrictions.
9. Keep Qt Quick/C++ and the existing small helpers. Do not add another runtime,
   dependency injection framework, image cache database, or automatic disk cache.
10. Only synthetic/disposable files may be written in automated tests. Use fake
    commands and injected failures for disconnections. Never manipulate the
    user's real drives, shell configuration, language setting, or clipboard in
    automated tests. No unattended push, release, or CI run.

## 1. Establish agent documentation and deterministic test boundaries

**Files:** add `apps/image-viewer/AGENTS.md` and
`apps/image-viewer/docs/architecture.md`; extend `tests/` and its `.pro` files.
Initially leave production behavior unchanged.

The short local AGENTS file must state:

* This subtree is a standalone C++20/Qt Quick application, not a shell plugin.
  Describe the existing architecture without weakening root USB safety rules.
* Exact commands and working directories for build, tests, CLI inspection,
  package checks, and QML/native smoke tests. State that `--rotate` overwrites
  its argument and is only for a disposable fixture during development.
* A source map: `Backend` orchestrates, `ImageIO` reads/writes, `PhotoItem` owns
  scene-graph resources, `Main.qml` orchestrates user interactions, translator
  and theme provide integration, qmake/resources lists control build inclusion.
* The invariants above, a short completion checklist, and links to architecture,
  test fixtures, resource policy, and the benchmark procedure.
* No real media fixtures, credentials, home paths, device IDs, generated
  binaries, or build logs in source. No changes to `/usr/share/omarchy`.
* Update the relevant architecture contract and translation catalogs whenever
  behavior or public properties change. Do not create a second contradictory
  backlog in AGENTS.

Architecture documentation must explain the fields currently named `m_path`,
`m_target`, `m_requested`, `m_reading`, `m_serial`, `m_generation`, `m_check`, and
`m_turns`, and include a transition table for open/navigation/detail/cache
validation/save/copy/picker/shutdown. Document save phases and buffer lifetimes.
Keep it synchronized as later packages rename/extract responsibilities.

Add **small** test seams at external boundaries, not wrappers around every Qt
class: controlled read/scan completions, helper execution, commit syscalls, and
portal transport. Production defaults still call the current real code. A fake
job executor must permit A/B completions in arbitrary order; tests must not
depend on file size or sleeps to trigger a race. Give fixtures named paths,
dimensions, colors, and stamps so assertions identify which image won.

Split tests by responsibility as they grow: image I/O/save, backend scheduling,
QML interactions/rendering, integration/translation. Include `translator.cpp`
in a test target; currently `tests/viewer-tests.pro` does not compile it. Inject
theme/language source paths for tests so no test edits live configuration.

**Acceptance:** existing checks remain green; documentation identifies where
each invariant is enforced; a controlled fake can hold and release a read
without blocking the test GUI event loop. No large backend rewrite in this step.

## 2. Fix translation concurrency before adding more worker behavior

**Evidence:** `translator.cpp:35–43` assigns/reads `m_strings` without a lock;
`imageio.cpp:27–29` translates in I/O workers; `backend.cpp:376–394` also calls
`tr()` in an operation worker. Qt's [QTranslator contract](https://doc.qt.io/qt-6/qtranslator.html#translate)
requires thread-safe translation lookup.

1. Add a read/write lock for the catalog, or an equally explicit immutable
   snapshot publication mechanism. Prefer `QReadWriteLock` for this small class.
2. Parse the replacement catalog outside the lock. Under a write lock, compare
   and replace the catalog. Release the lock **before** emitting `changed`, since
   retranslation calls back into the translator.
3. Hold a read lock for the lookup and returned QString copy. Do not return a
   reference into the mutable catalog. Keep locale/layout changes on the GUI
   thread; review `QLocale::setDefault` use while workers run and prefer an
   explicit locale for worker operations that need one.
4. Add a test-only constructor/input seam to reload catalogs from a temporary
   directory or supplied data. Test repeated concurrent lookups and reloads,
   missing keys, malformed JSON, fallback language, and signal emission outside
   the lock. Run ThreadSanitizer where available; report if unavailable.
5. In package 10 move user-facing worker failure formatting to the GUI thread
   with stable error codes. Keep the translator itself safe regardless: Qt or
   future code can still call it from another thread.

**Acceptance:** concurrent lookup/reload is synchronized, catalog errors have
a defined fallback, there is no lock/retranslation deadlock, and tests exercise
the actual production translator. Do not claim a probabilistic stress test
alone proves absence of races.

## 3. Recover navigation and preserve useful folder snapshots

**Evidence:** `backend.cpp:43–51` resets target to displayed path on failure;
`neighbor()` at 133–137 uses the displayed path again once loading is false.
`imageio.cpp:204–219` cannot report scan failure separately from an empty list.

1. Introduce an explicit navigation cursor independent of the displayed path.
   On a failed target, keep the cursor at that failed entry while retaining the
   displayed image. Next should target the following entry; Previous should
   target the preceding one. Do not automatically walk an entire corrupt folder.
2. Preserve latest-target coalescing during rapid key presses. Returning to the
   already displayed image may complete without decode. Rotation/copy/save/send
   remain tied to the displayed file; keep the failure notice naming the failed
   target so this distinction is understandable.
3. Reset/reconcile cursor on explicit open, folder change, successful refresh,
   and deletion. Specify first/last entry behavior, including a failed initial
   open with no displayed frame. Keep no-wrap navigation.
4. Store an index or a path-to-index map when installing a sorted folder
   snapshot. Avoid repeated `QStringList::indexOf` inside previous/next queries.
   Preserve natural sort and its deterministic tie-breaker.
5. Return a `FolderSnapshot` containing entries and scan status/error. Detect
   open/enumeration failures on the worker side. Retain the last successful
   snapshot on failure rather than reporting a fresh empty directory. Do not
   mark failed scans as successful freshness updates. Keep new-folder failure
   separate from an old folder's snapshot; never navigate old siblings under a
   new directory identity.
6. Use a monotonic elapsed clock for refresh age instead of wall-clock time.
   Schedule retries only through explicit refresh/focus policy, with coalescing;
   do not create a rapid error/rescan loop.

**Tests:** valid/corrupt/valid in both directions; consecutive corrupt entries;
first/last/all-corrupt folders; deleted target; failed first open; A→B→C with
completions out of order; refresh changing sort positions; failed refresh;
genuinely empty folder; old scan completing after folder change. Check the
displayed frame, navigation cursor, enabled actions, and status independently.

**Acceptance:** two settled Next presses move past one corrupt entry; no failed
read clears the good frame or changes its edit target; a large-folder navigation
step does not scan or linearly search the complete list.

## 4. Bound JPEG helper execution and make save phases explicit

**Evidence:** `imageio.cpp:168` and `318` wait forever. `Main.qml:32–35,58–62`
prevents ordinary closing while `viewer.operating` is true.

1. Extract the duplicated QProcess lifecycle into one small helper runner. Use
   a monotonic deadline, bounded stdout/stderr capture, and explicit outcomes:
   start failure, timeout, cancellation, crash, nonzero exit, success. Preserve
   jpegtran's current perfect-rotation fallback and Exiv2 verification.
2. Start with a named 60-second helper deadline, injectable to milliseconds in
   tests. This is a starting policy, not an image-decode deadline; adjust only
   with measured large-file evidence. Drain output while running and retain at
   most a documented diagnostic tail (for example 64 KiB per stream).
3. On timeout/cancel before commit, stop only this application's own helper,
   request termination, and escalate after a short bounded grace period when
   necessary. Never kill another application or a system-wide process name.
   Do not mistake `waitForFinished(timeout)` for termination of the child.
4. Model save phases: validating, staging/transforming, verifying staged data,
   committing, verifying commit. A cancellation accepted before commit must
   prevent entry into rename. Once commit begins, do not report “cancelled,
   original unchanged” without verification. Do not start another mutation
   while the previous result is uncertain.
5. Keep edit state and the original bytes on definite precommit failure. Return
   a useful typed reason and clear the UI operation state. Handle exceptions at
   job boundaries and when retrieving futures so ordinary worker failures do
   not escape GUI callbacks; do not promise recovery from arbitrary process OOM.
6. Make close behavior phase-aware. During a cancellable helper operation, close
   can request cancellation and then return to the unsaved-edit flow. During
   commit verification, show why closing is waiting. A stalled storage syscall
   is a separate limitation covered in package 6, not solved by a QProcess timer.

**Tests:** fake helper never finishes until cancelled; ignores initial terminate;
fails to start; crashes; exits nonzero; floods stderr; returns the perfect-rotate
fallback diagnostic; produces invalid/truncated staged output. Assert original
bytes, edit state, temporary cleanup, final operation state, and one completion
signal. Use fake commit phases to prove cancellation never becomes a second save.

**Acceptance:** a normally schedulable hung helper cannot keep the UI in an
operation indefinitely; fake-helper failure tests finish under a deterministic
test deadline. A child stuck in uninterruptible kernel I/O may still outlive a
deadline—do not hide this platform limit or block the GUI trying to reap it.

## 5. Give the portal request a complete lifecycle

**Files:** `backend.cpp:404–458`, `backend.h:operating()`, QML close flow; extract
a small `FilePicker` QObject if needed to keep D-Bus state out of Backend.

1. Track request identity, predicted/returned handle, signal subscription, and
   completion exactly once. Keep the existing pre-subscription approach.
2. Check both `bus.connect` results, including the replacement-handle branch.
   Distinguish method-call failure, service disappearance, invalid result, user
   cancellation, and success. Subscribe to portal service-owner loss.
3. Add application-side cancel/close using the portal Request `Close` method.
   Clear local state even if the portal cannot be contacted, and invalidate late
   replies using the captured request identity. Do not cancel a new picker
   because an old method watcher finally returned.
4. Do not impose a short timeout on a healthy dialog waiting for its user.
   Bound the method handshake; provide explicit cancel/close and service-loss
   recovery for the longer interaction.
5. An empty/malformed URI list must produce a specific error, not silently select
   an empty path. Continue accepting only local-file URLs/mounted paths.

The [portal Request specification](https://flatpak.github.io/xdg-desktop-portal/docs/doc-org.freedesktop.portal.Request.html)
says application-side Close does not emit Response; local completion must not
wait for that signal.

**Tests:** expected and changed handles; rejected signal subscription; Response
before method reply; no Response followed by user cancel; service disappearance;
late Response after cancellation/new request; invalid URI; successful local URI.
Use an isolated test bus/fake transport, never the live desktop picker in unit
tests. After each terminal path, open/close must work again.

## 6. Separate execution capacity from result cancellation

**Evidence:** all four `QtConcurrent::run` calls use the global pool
(`backend.cpp:217,234,317,376`). `detail()` also uses `m_loader`, so a slow detail
read blocks a later cold navigation until it returns.

1. Use explicit bounded execution lanes: foreground reads, directory scans,
   background work, and user operations. A small owned scheduler with one worker
   per lane is sufficient; do not create one thread per request.
2. Route detail upgrades through the background lane together with speculative
   reads. Prioritize active-image freshness/detail over new neighbor prefetch.
   Preserve one active foreground read plus at most one latest pending target;
   one active scan plus one latest pending scan; bounded/coalesced background
   intentions; and one user operation. Do not queue every zoom event or keypress.
3. Invalidate superseded work and add cooperative checks between enumeration,
   qualification, decode, resize, and publication where safe. Drop obsolete
   work before expensive next stages. Do not detach unlimited replacement jobs
   whenever an old syscall is blocked.
4. Suppress launching new expensive speculative work during a foreground load,
   copy/save, or memory pressure. An already running decoder may finish; account
   for it. Coordinate this with the resource policy in package 7.
5. Define scheduler lifetime explicitly. Late workers must not use destroyed
   UI, backend, or translator objects. Do not call pool wait/drain in the GUI
   event loop. Add a subprocess-level shutdown test with controlled blocked
   work and record both window-close and process-exit behavior.

**Limit of this work package:** Qt futures do not interrupt arbitrary filesystem
calls, and pool destruction waits for running work. These limits are documented
by [QFuture](https://doc.qt.io/qt-6/qfuture.html) and
[QThreadPool](https://doc.qt.io/qt-6/qthreadpool.html). Dedicated lanes fix
cross-lane starvation; they do not guarantee another foreground read or process
exit while a foreground filesystem syscall is permanently stuck. Keep that
remaining limit explicit in README and validation results. If hard deadlines
for mounted-file reads become a requirement, use a separately designed process
isolation follow-up; do not improvise unsafe thread termination in this pass.

**Tests:** hold background/detail then open a cold image; hold scan then open a
known path; rapid targets/direction changes; foreground A blocked while B/C
coalesce; stale detail after successful save; stale prefetch after refresh;
operation and background overlap; destruction with late completions. Assert
maximum active/pending job counts as well as final image identity. Use small
worker capacity deliberately so tests do not depend on CPU core count.

## 7. Budget transient decode memory and release obsolete detail

**Evidence:** `imageio.cpp:245–266` may decode fully before scaling; allocation
behavior depends on the codec. `backend.cpp:267–277` only upgrades and caps by
edge, not bytes. `Main.qml:125–129` Fit never requests downshift.

1. Keep the 48 MiB neighbor cache budget. Document additional buckets: active
   preview, optional active detail, in-flight decode/rotation/verification,
   renderer allocations, and clipboard. Do not call 48 MiB the app memory limit.
2. Introduce a small `ImageResourcePolicy` with named constants and checked
   dimension/byte arithmetic. Account for 64-bit/high-depth images and multiple
   working buffers. Query/report Qt's existing image allocation limit; **do not
   disable it or change the process-global limit from individual worker jobs**.
3. Use a starting active-detail output budget of 64 MiB, alongside the existing
   8192 edge ceiling. Fit the requested detail size to both constraints. Treat
   this as a retained-output cap, not a guarantee about decoder internals.
4. Before speculative work, inspect dimensions/format off-thread. For codecs
   without a demonstrated cheap scaled decode, skip speculation when estimated
   full-image working memory exceeds a conservative named threshold (start with
   64 MiB). Viewing that file on explicit navigation remains allowed under the
   foreground policy. `supportsOption(ScaledSize)` alone is not proof of a
   bounded decoder working set. Unknown dimensions must not cause unlimited
   speculative allocation.
5. Retain or produce a fit preview separately from the optional detail buffer.
   On Fit or a sustained lower-resolution demand (start with a 500 ms debounce),
   switch rendering back to the preview, invalidate now-unwanted detail work,
   and release the large frame and texture references. If a downsample is
   needed, do it off-thread; do not reread the share just to shrink a good frame.
   Preserve zoom/rotation, source stamp, and save qualification.
6. Discard the separate detail buffer on navigation. When appropriate, cache
   the fit preview rather than dropping the old image from the neighbor cache
   merely because the last displayed frame exceeded `m_edge`.
7. Avoid repeated failed detail decodes for the same stamp/size. Record the
   failure until an explicit retry, source change, or lower feasible request;
   show a useful reason when requested detail cannot be supplied.
8. Record full-resolution operation estimates and outcomes. Never silently
   downscale clipboard/save output to fit a memory budget. If unsupported size
   prevents the operation, explain it and leave the edit intact.

Qt's [image reader documentation](https://doc.qt.io/qt-6/qimagereader.html#setAllocationLimit)
describes allocation limits and format-dependent scaling; neither is an
aggregate process memory bound. The starting thresholds above are conservative
implementation defaults to validate, not numbers inferred from the single PNG
measurement.

**Tests/measurements:** JPEG native scaling; PNG full decode then shrink;
large/high-depth TIFF or equivalent supported fixture; extreme dimensions;
unknown-size decoder result via fake; detail→Fit; repeated detail failure;
clipboard retains full resolution; cache bytes never exceed budget. Repeat
browse/zoom/Fit cycles and show owned detail bytes return to zero after settling.
RSS may stay above its initial value due to allocator caching; require no
unbounded live-buffer growth rather than an exact RSS return to baseline.

## 8. Make render failure safe without breaking texture ownership

**Evidence:** `photoitem.cpp:10–14,49–55` attaches an image node before a texture
exists and returns it when texture creation fails. Qt explicitly requires a
texture before an image node is rendered in the
[QSGImageNode documentation](https://doc.qt.io/qt-6/qsgimagenode.html#setTexture).

1. Create/install the first usable texture before returning a new renderable
   image node. Handle failure of node creation as well as texture creation.
   Destroy an unusable new node and return null; do not leave a textureless child
   in the scene graph.
2. Distinguish failure to present a new photo from failure to upgrade detail for
   the same photo. A failed detail upload can keep its lower-resolution frame.
   A failed new-photo upload must not silently display old pixels under a new
   filename with Save enabled. Report a queued, generation-tagged presentation
   failure to the GUI and show a recoverable error/placeholder or retain the
   complete prior displayed-photo state consistently.
3. Keep texture creation/destruction on the scene-graph thread. Validate results
   after context recreation, hide/show, and resize. Coalesce retries; do not
   spin allocation attempts every frame after a persistent failure.
4. Test null allocation with a minimal injected factory or node-construction
   helper, and test normal rendering using both software and an available RHI
   backend. Verify first image, image replacement, detail replacement, clear,
   and scene-graph recreation.

**Do not “fix” an unproven texture leak.** The current code sets
`setOwnsTexture(true)`; [Qt's default image-node implementation](https://github.com/qt/qtdeclarative/blob/dev/src/quick/scenegraph/util/qsgdefaultimagenode.cpp)
deletes the previous owned texture on replacement. Adding another unconditional delete can
double-free it. Consult the target Qt implementation and use lifecycle tests.
The review found an allocation-failure path, not a demonstrated texture leak.

**Acceptance:** no renderable node lacks a texture, old pixels cannot be
mistaken for the new actionable image, repeated replacement has bounded live
resources, and normal pan/zoom does not recreate the texture unnecessarily.

## 9. Complete the save failure matrix and durability contract

**Evidence:** `imageio.cpp:282–406` checks identities, qualifies metadata, stages
and verifies output, fsyncs the staged file, renames, and verifies the target.
These are worth preserving. It does not fsync the parent directory. Current
tests cover successful PNG/JPEG writes and a few pre-write refusals, not commit
failures, ambiguous commits, or most qualification boundaries.

1. Introduce narrow file-operation seams for the staging/commit boundary so
   tests can inject failures without filling a disk, changing a real mount, or
   requiring elevated privileges. Keep production syscalls straightforward.
2. Represent outcomes explicitly: not committed, committed/verified, commit
   uncertain. Track durability separately if needed; it is not the same as
   image identity. Adapt Backend and CLI consumers together.
3. Open and retain a parent-directory descriptor for durability work; after
   verified replacement, synchronize the directory where supported. The
   [Linux fsync documentation](https://man7.org/linux/man-pages/man2/fsync.2.html)
   explains why file fsync alone does not persist its directory entry.
4. If rename committed but directory fsync fails/is unsupported, never report
   “original unchanged” or invite a blind retry. Return the verified saved stamp
   with an explicit durability warning. Distinguish unsupported directory sync
   from an I/O failure and document limitations on network filesystems.
5. Preserve final precommit source checks and postcommit identity verification.
   Document that stat/rename is optimistic conflict detection, not an atomic
   compare-and-swap against other clients. Do not claim strict cross-client
   conflict prevention.
6. Test unsupported format/metadata boundaries before touching qualification
   logic: APNG; PNG unknown chunks before and after IDAT; text/profile/density;
   transparent/indexed/high-depth PNG; JPEG unknown APP markers, progressive
   files and metadata beyond the first scan; EXIF orientations 1–8 with turns
   1–3; odd sizes; distinct X/Y density; comments and ICC. If a newly tested case
   cannot preserve its required semantics, make it view-only with a reason.
   Do not assume these cases are all currently broken; they need qualification.

**Failure matrix:** input changed before open; descriptor/path mismatch; change
during decode/encode; precommit change; symlink/hardlink; owner/mode/xattr
refusal; temporary creation/write/flush/fsync failure; helper failure; invalid
staged output; definite rename refusal; rename reports error but replacement
exists; post-rename stat unavailable; replacement identity mismatch;
directory-sync failure. Verify original hashes for precommit failures, staged
cleanup, edit preservation, save disablement after uncertainty, and successful
reload recovery. Include QML Save→Continue: continuation executes once on
verified success and never after an uncertain/failed save.

**Acceptance:** every result maps to a defined UI/CLI state, no test mutates real
media, and a potentially committed rotation is never automatically retried.

## 10. Replace fragile message/state coupling and extract by responsibility

**Evidence:** `Main.qml:101–105` chooses status duration by comparing translated
strings; `m_operationType`/`runOperation` use string operation names; several
booleans jointly encode state. `Main.qml` contains viewport logic, dock, menus,
prompts, help, and action continuation in 598 lines; Backend construction holds
four substantial completion handlers. Size alone is not a defect, but the
implicit contracts make asynchronous changes difficult for agents to verify.

1. Add small enums/value types where behavior depends on a distinction:
   operation kind; status severity/code; save outcome; request identity. Avoid
   one giant enum for unrelated concurrent tasks. Centralize GUI-thread error
   formatting; keep technical diagnostics separately bounded and optional.
2. Expose status severity and recovery action separately from localized text.
   Success messages may auto-dismiss; a save conflict, uncertain commit, or
   persistent resource failure should remain discoverable until dismissed or
   resolved. Surface why Save is blocked, including in the unsaved-edit dialog
   when its Save button is disabled.
3. Keep one action-continuation owner for save/discard/cancel and subsequent
   navigation/open/send/close. Clear continuations on cancellation/failure and
   prevent unrelated late success signals from executing them.
4. Extract `ImageViewport.qml`, then `ViewerDock.qml` and an unsaved-edits
   component only when their inputs/outputs can be explicit. Leave application
   action orchestration in Main. Pass properties/signals rather than reaching
   through arbitrary IDs in the parent. Update `resources.qrc` and preserve
   stable `objectName` test hooks and the current visual design.
5. Move watcher completion lambdas into named handler methods; keep scheduling,
   picker, and helper responsibilities in their small dedicated components.
   Do not rewrite Backend as a generic event framework. Replace one boundary at
   a time after its regression tests exist.
6. Add catalog coverage/placeholder tests for all user-facing strings and English
   fallback. Test a language change while a status/unsaved dialog is present.
   The existing Swedish/Danish catalogs omit many I/O failure messages.

**Acceptance:** UI policy never compares translated message text, blocked-save
reasons remain accessible, action continuation is tested for success/failure/
cancel, and new components have explicit small APIs documented for agents.

## 11. Make builds and packages reproducible from declared source

**Evidence:** `pkgbuild/PKGBUILD:12–20` has empty source/checksum arrays and builds
from `$startdir/..`; package functions reuse a source-tree build directory.
`apps/image-viewer/.gitignore` excludes build/package outputs, but neither it nor
root `*.log` excludes the already visible `*.log.1` / `*.log.2` artifacts.

1. Ignore rotated build logs and other generated package byproducts. Do not
   silently delete existing user files; simply keep generated files out of the
   source set. Check staged/untracked files before committing.
2. Make source builds support an explicit output directory while preserving
   the current convenient default. Package builds/tests must use fresh separate
   output directories, not reuse `apps/image-viewer/build` objects.
3. Declare a self-contained local source archive and checksum for makepkg. A
   deterministic staging script should collect an explicit allowlist of app
   source, tests, resources, launcher/icon, and LICENSE, excluding build data.
   Do not use `git archive HEAD` here: reviewed viewer source is untracked.
   Build/check/package from `$srcdir`/`$pkgdir` using only that staged source.
4. Declare/check the minimum Qt version actually needed by QDirListing and the
   QML APIs in use. Do not claim compatibility based only on “Qt 6”. Keep C++20
   and qmake unless a separate requirement warrants changing them.
5. Keep app/package version values synchronized through one small source of
   truth or a consistency check. Validate desktop/icon resources and the
   advertised MIME types against the intended installed decoder set. Do not
   remove format dependencies solely to lower an apparent library count.
6. Make `bin/test` retain its headless default but permit an explicitly selected
   native/rendering test mode. It currently forces software rendering and thus
   cannot validate the production override path.

**Acceptance:** a package can build/check from a fresh staging directory without
access to an adjacent checkout; two clean builds do not reuse stale objects;
source artifacts exclude logs and machine paths; source and packaged tests
pass. Any install/update/uninstall verification must preserve unrelated shell
configuration and file associations. No package installation, publication, or
release is authorized by this review-only request.

## 12. Measure before tuning rendering, decoding, and idle work

**Files:** add a reproducible benchmark script/procedure and a sanitized results
document. Keep synthetic fixtures generated outside the repository/build source.

1. Record Qt/app build, renderer, viewport/DPR, image dimensions/format/depth,
   cache state, and local versus mounted fixture location without publishing
   personal paths. Use a monotonic timer for events.
2. Instrument request start, decode finish, cache hit/miss, detail start/drop,
   frame installation, and actual frame presentation. The existing
   `IMAGE_VIEWER_TRACE` `photo-ready` timestamp occurs before rendering, so it
   cannot prove visible-switch latency. Include request IDs, not full paths.
3. Record median/p95 cold first paint, warm next/previous, rapid direction
   changes, CPU during pan/zoom, peak RSS, cache/current/detail/clipboard bytes,
   active/pending job counts, and read bytes where meaningful. Use at least 30
   settled navigation samples after a documented warmup. Do not flush the
   machine's global filesystem cache to manufacture a cold run.
4. Compare software and one available hardware renderer on the same fixtures
   and viewport. Preserve user renderer overrides. Change the default only if
   results show a material benefit with equivalent visual/functional behavior;
   otherwise record why software remains. Include large photos, transparency,
   EXIF rotation, repeated zoom/Fit, and a scaled display.
5. Benchmark a long synthetic directory and broad decoder discovery separately.
   Check whether startup work in `ImageIO::formats()` and repeated write
   qualification materially delay viewing. Only then consider caching stable
   helper discovery or separating view decode from capability checks; a Save
   must always revalidate capability at operation time.
6. Measure idle wakeups. `Theme` polls `hyprctl` every 15 seconds and emits
   `changed` even for unchanged values. Suppress redundant notifications and
   debounce watcher bursts if measurements show needless work. Keep live theme
   updates and tests for atomic file replacement/missing files.

**Acceptance:** publish before/after numbers and caveats, not “faster” based on
subjective observation. Retain the original product target of warm visible
switching under 100 ms on the recorded reference workload. No statistically
meaningful regression beyond a predeclared 10% comparison tolerance without
an explicit explanation. Memory success means bounded retained resources and
lower unnecessary overlap, not fitting the whole process into 48 MiB. If no
authorized representative mounted fixture or hardware backend is available,
mark that part unverified; do not manufacture a pass.

## Execution order and final validation

Suggested commit-sized sequence: documentation/test seams → translator →
navigation/scan recovery → helper/save phase recovery → picker lifecycle →
worker scheduling → memory policy → rendering failures → save failure matrix/
durability → typed status and QML extraction → packaging → benchmark-informed
tuning. Packages 2–5 should not wait for cosmetic refactoring or optimization.
For each package, add a regression demonstrating the old behavior, implement
the focused change, and run its target tests before proceeding.

Run from the repository root at completion:

```sh
./apps/image-viewer/bin/build
./apps/image-viewer/bin/test
node --test tests/*.test.cjs
for script in apps/image-viewer/bin/* apps/image-viewer/pkgbuild/PKGBUILD \
  plugins/usb-storage/bin/* plugins/usb-storage/scripts/usb-storage; do
  bash -n "$script" || exit
done
omarchy plugin validate plugins/usb-storage
git diff --check
git status --short --untracked-files=all
```

Add new scripts to syntax checks and run the new clean-package checks. Run the
asynchronous tests repeatedly with controlled completion orders; use sanitizers
where available and record their limits. Do not rely solely on nine existing
Qt test functions or a successful source build.

For any implemented QML changes, inspect the installed UI and shell/application
logs, and embed screenshots of changed UI in the implementation report as
required by the root AGENTS instructions. Verify native Wayland, keyboard/dock/
menu behavior, error messages, small viewport, scaled display, unsaved edits,
real portal cancellation, and clipboard paste using disposable content. Use the
relevant Omarchy skill if changing user desktop configuration; this project
should ordinarily not need such changes. Package installation must be within
the implementation task's authorization; this plan alone does not install it.

The implementation report must include: completed finding IDs; precise remaining
limitations (especially blocked kernel I/O and mounted-filesystem verification);
test results; memory/latency evidence; screenshots when applicable; and links
to updated agent documentation. Update the old product plan's status/link so a
future agent does not mistake finished work for an unimplemented design.

## Explicitly deferred

Do not add process-isolated decoders, tiled huge-image rendering, disk previews,
animation playback, a new build system, or a general plugin framework in this
pass. Process isolation deserves a separate design if hard read/exit deadlines
are required. Tiling may be appropriate if measured demand for very large
actual-size viewing exceeds the bounded detail policy. Do not expand the write
format set until the existing JPEG/PNG qualification and failure matrix is
reliable.
