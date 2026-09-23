<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Platform, integration and cross-cutting services

**PROPOSED — hypothetical reference, not implemented APIs.** Names and members
are design sketches. Each entry inherits the [shared architecture and completion
gates](00-architecture.md); its local check is an additional acceptance example.
Priorities express dependency/value order, not effort estimates. A service/model
name need not become a visual widget or a separately constructible QML type.

[Book index](../README.md) · [All proposals](10-proposal-index.md) · [Roadmap](../roadmap.md)

Current platform support consists of Qt-based controls/rendering, the optional
QindaQt theme bridge, build/package rules and a preview tool. The services below
would make platform and backend differences explicit. They supplement the common
contracts; this is not evidence that all platforms or third-party formats are
already supported.

<a id="plt-01"></a>
## PLT-01 — WindowHost

**Priority:** P1. **Purpose:** Coordinate native windows and separate views of documents.

**Contract sketch:** Window/view IDs, document association, screen identity, geometry, focus and role; open/move/close/restore with capability checks.

**Dependencies and boundary:** Qt/platform window adapter, DocumentManager and workspace storage.

**Completion check:** Detach/reopen a document view, remove a monitor and restore reachable windows without duplicating document state.

<a id="plt-02"></a>
## PLT-02 — DetachedPanelHost

**Priority:** P2. **Purpose:** Bridge DockModel panel IDs into genuine native floating windows.

**Contract sketch:** Panel/window mapping, local/global geometry conversion, dock/undock requests and host transfer; content lifetime remains explicit.

**Dependencies and boundary:** Existing DockModel plus WindowHost; render/context transfer is validated per platform.

**Completion check:** Move a panel between windows/displays without losing its editor state, then restore with the secondary screen absent.

<a id="plt-03"></a>
## PLT-03 — FileDialogAdapter

**Priority:** P1. **Purpose:** Standard async open/save/folder selection and portal/native integration.

**Contract sketch:** Mode, MIME/type filters, initial URI, selected targets and canceled/error outcomes; supported-location capabilities are exposed.

**Dependencies and boundary:** Platform dialog/document-portal providers; apps receive URIs rather than assuming local paths.

**Completion check:** Native and fallback dialogs return equivalent outcomes; cancel preserves document state and unsupported locations are explained.

<a id="plt-04"></a>
## PLT-04 — PrintAdapter

**Priority:** P2. **Purpose:** Submit document output to platform print services.

**Contract sketch:** Capabilities, print ticket, page provider, job handle and success/failure/cancel states; coordinate with PrintPreview.

**Dependencies and boundary:** Document renderer and printer/spooler adapter; the backend decides supported options.

**Completion check:** A failed/canceled print is not reported as success; rendered page ranges and options match the validated ticket.

<a id="plt-05"></a>
## PLT-05 — DesktopIntegration

**Priority:** P2. **Purpose:** OS-facing app behaviors such as file associations and recent documents.

**Contract sketch:** Open-document requests, activation, recent-item registration, drag/clipboard conventions and task/status integration through capabilities.

**Dependencies and boundary:** Platform providers and DocumentManager; native menus/tray integration are optional adapters.

**Completion check:** External file-open and second-instance activation reach the correct session without bypassing save/close policy.

<a id="plt-06"></a>
## PLT-06 — AccessibilityAdapter

**Priority:** P0. **Purpose:** Reusable semantics and actions for custom composite editors.

**Contract sketch:** Stable accessible object IDs, roles/names/states/values, child navigation, actions and announcement queue; include virtualized content.

**Dependencies and boundary:** Qt accessibility/platform bridge plus domain text/object/time/grid adapters.

**Completion check:** Real screen-reader users can select/edit/navigate a grid, canvas and timeline with stable identity after model changes.

<a id="plt-07"></a>
## PLT-07 — LocaleService

**Priority:** P1. **Purpose:** Consistent numbers, dates, units, strings and directionality.

**Contract sketch:** Locale, time zone, numbering/calendar choices, format/parse policies and content-versus-chrome direction; expose change notifications.

**Dependencies and boundary:** Qt/platform locale services and domain formatting adapters.

**Completion check:** Switch locale with dirty edits and verify canonical values stay intact; RTL chrome and bidirectional document content remain usable.

<a id="plt-08"></a>
## PLT-08 — FocusManager

**Priority:** P0. **Purpose:** Predictable focus scopes and restoration across composite editors/popups.

**Contract sketch:** Track logical focus target, scope entry/exit, modal ownership and restore targets by stable ID; permit documented overrides.

**Dependencies and boundary:** Qt focus primitives, DocumentManager and accessibility layer.

**Completion check:** Open/close dialogs, detach views and remove the focused item without losing keyboard access or stealing IME focus.

<a id="plt-09"></a>
## PLT-09 — GestureRouter

**Priority:** P1. **Purpose:** Arbitrate mouse, touch, pen, wheel and temporary navigation gestures.

**Contract sketch:** Device/modifier/tool context, accepted gestures, capture ownership and cancellation; map input to tool transactions.

**Dependencies and boundary:** Qt pointer handlers and platform input adapters; StylusHandler remains one input source.

**Completion check:** Pen drawing, touch panning and mouse fallback cannot simultaneously create duplicate edits; lost grabs terminate gestures cleanly.

<a id="plt-10"></a>
## PLT-10 — RenderSurfaceAdapter

**Priority:** P1. **Purpose:** Lifecycle boundary for custom raster/GPU/domain renderers.

**Contract sketch:** Visible region, revision, transform, device ratio, color context, damage and texture/resource handles with ownership/thread declarations.

**Dependencies and boundary:** Qt scene graph/render API and external renderer; backend resources stay outside QML object ownership ambiguities.

**Completion check:** Resize, switch backend or lose a graphics device without stale handles, crashes or document loss.

<a id="plt-11"></a>
## PLT-11 — PluginHostAdapter

**Priority:** P2. **Purpose:** Expose plugin capabilities, parameters and editor lifecycles to apps.

**Contract sketch:** Plugin descriptors/instances, parameter IDs, state serialization, editor-open and failure signals; processing capabilities are separate.

**Dependencies and boundary:** Audio/video/other plugin host implementation with explicit isolation/version policy.

**Completion check:** A missing or failed plugin retains its serialized state and identity; reopening a project permits later recovery/replacement.

<a id="plt-12"></a>
## PLT-12 — ExtensionRegistry

**Priority:** P2. **Purpose:** Register optional controls, tools, commands, renderers and format providers.

**Contract sketch:** Versioned descriptors, required capabilities, dependencies and lifecycle hooks; validate registration and unload policy.

**Dependencies and boundary:** Application/plugin loader and stable ABI/API contracts; optional extensions cannot mutate core contracts implicitly.

**Completion check:** An incompatible extension is rejected with diagnostics; removing one preserves document data and accessible fallback views.

<a id="plt-13"></a>
## PLT-13 — ScriptConsole

**Priority:** P2. **Purpose:** Inspect and automate applications through deliberate public commands.

**Contract sketch:** Command/model facade, script execution state, output/errors and cancellable jobs; define exposed capabilities and persistent macro format.

**Dependencies and boundary:** Chosen scripting runtime and application policy; scripts operate through domain commands rather than arbitrary delegate internals.

**Completion check:** Replay a recorded command sequence on fixtures and undo semantic edits; canceled scripts leave consistent documents.

<a id="plt-14"></a>
## PLT-14 — CollaborationAdapter

**Priority:** P3. **Purpose:** Connect local commands/documents to a shared editing backend.

**Contract sketch:** Document/revision IDs, remote operations, presence and conflict states; declare offline/merge/undo semantics and authorization failures.

**Dependencies and boundary:** Collaboration protocol, durable log and domain merge model; toolkit renders state but does not supply a universal merge algorithm.

**Completion check:** Two clients edit overlapping content, disconnect and reconnect with reproducible convergence or explicit conflicts and no silent loss.

<a id="plt-15"></a>
## PLT-15 — PresenceLayer

**Priority:** P3. **Purpose:** Show collaborators' cursors, selections and activity.

**Contract sketch:** User/session IDs, target ranges/objects, view/time context and freshness; display only supported coordinates and permissions.

**Dependencies and boundary:** CollaborationAdapter and domain selection maps; user colors need readable non-color labels.

**Completion check:** Remote selection follows edited content and disappears when stale; it never changes local selection or document colors.

<a id="plt-16"></a>
## PLT-16 — VersionBrowser

**Priority:** P2. **Purpose:** Inspect, compare and restore document revisions.

**Contract sketch:** Revision IDs, authors/times, previews, differences and restore/branch requests; keep unsaved local state visible.

**Dependencies and boundary:** Versioned storage, DiffView and DocumentSession; domain defines merge/restore behavior.

**Completion check:** Restore an older revision without silently destroying an unsaved current one; compare references the correct immutable versions.

<a id="plt-17"></a>
## PLT-17 — ImportDialog

**Priority:** P1. **Purpose:** Preview and configure ingestion into a document or library.

**Contract sketch:** Detected format, adapter capabilities, options, preview, warnings and import-as/link/embed choices; commit via a job/transaction.

**Dependencies and boundary:** FormatRegistry, AssetProvider and domain import backend.

**Completion check:** Unsupported content is reported before commit; cancellation removes temporary work and accepted imports create consistent references.

<a id="plt-18"></a>
## PLT-18 — ExportDialog

**Priority:** P1. **Purpose:** Validate output format, scope, settings and destination.

**Contract sketch:** Format capabilities, preset, range/selection, preview and compatibility/loss warnings; submit a tracked export job.

**Dependencies and boundary:** FormatRegistry, render/encode adapter and JobModel; success requires validated output.

**Completion check:** Changing format removes invalid options; failed or canceled export never masquerades as a complete deliverable.

<a id="plt-19"></a>
## PLT-19 — DeviceSettingsPanel

**Priority:** P2. **Purpose:** Configure audio/MIDI/video/control hardware through capabilities.

**Contract sketch:** Stable device/port IDs, rates/layouts/buffer options, availability and pending/active configurations; apply/test/rollback requests.

**Dependencies and boundary:** Device engine and OS APIs; realtime reconfiguration occurs outside the UI thread.

**Completion check:** Hot-unplug during recording/configuration reports the actual engine state and provides recovery without selecting a different device by index.

<a id="plt-20"></a>
## PLT-20 — FormatRegistry

**Priority:** P1. **Purpose:** Discover and negotiate import/export/roundtrip capabilities.

**Contract sketch:** Format IDs, MIME/extensions, versions, operations, supported features and loss descriptors; select providers by explicit policy.

**Dependencies and boundary:** Independent parser/writer adapters; fixtures establish compatibility per operation/version.

**Completion check:** Render-only providers cannot advertise editing/roundtrip; unsupported features survive or produce a specific loss report.
