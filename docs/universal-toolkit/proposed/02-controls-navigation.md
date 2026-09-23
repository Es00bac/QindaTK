<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Controls, navigation and inspectors

**PROPOSED — hypothetical reference, not implemented APIs.** Names and members
are design sketches. Each entry inherits the [shared architecture and completion
gates](00-architecture.md); its local check is an additional acceptance example.
Priorities express dependency/value order, not effort estimates. A service/model
name need not become a visual widget or a separately constructible QML type.

[Book index](../README.md) · [All proposals](10-proposal-index.md) · [Roadmap](../roadmap.md)

Build these over [current inputs](../current/04-inputs.md), [actions](../current/03-text-actions.md)
and [shell controls](../current/05-shell-navigation.md). Reuse QtQuick.Templates
behavior and Theme roles. Extend an existing control where the semantics remain
the same; the proposed names below identify distinct behavior, not mandatory
one-file wrappers.

<a id="ctl-01"></a>
## CTL-01 — SplitButton

**Priority:** P1. **Purpose:** Primary action with a separate chooser/menu segment.

**Contract sketch:** Bind primaryCommand and menuModel; keyboard distinguishes default activation from opening choices; expose both accessible actions.

**Dependencies and boundary:** CommandRegistry and Menu; use a template interaction base.

**Completion check:** Primary clicks run once, the arrow only opens choices, and both operations work from keyboard.

<a id="ctl-02"></a>
## CTL-02 — MenuButton

**Priority:** P1. **Purpose:** Action-like button whose main purpose is opening a menu.

**Contract sketch:** menuModel/anchor/placement, open state and keyboard activation; selected choice can update label/icon through the command model.

**Dependencies and boundary:** Button/Menu/Popover and shared focus policy.

**Completion check:** Escape returns focus to the trigger; pointer and keyboard opening show the same available commands.

<a id="ctl-03"></a>
## CTL-03 — ToolShelf

**Priority:** P1. **Purpose:** Vertical or horizontal tool palette with mutually exclusive tools and subtools.

**Contract sketch:** tools, activeToolId, orientation and grouped flyouts; issue tool activation requests and show transient tool overrides.

**Dependencies and boundary:** ToolController from the canvas pack and CommandRegistry.

**Completion check:** Switch tools by shortcut or shelf, hold a temporary pan tool, then return to the prior tool consistently.

<a id="ctl-04"></a>
## CTL-04 — Ribbon

**Priority:** P2. **Purpose:** Grouped, contextual commands for Office-style interfaces.

**Contract sketch:** Tabs/groups describe commands, galleries and size priorities; contextual tabs follow selection; collapse/overflow and key tips remain navigable.

**Dependencies and boundary:** CommandRegistry, common controls and application capability/context models.

**Completion check:** Every command remains reachable at narrow widths and scaled text; contextual groups update without stealing text focus.

<a id="ctl-05"></a>
## CTL-05 — CommandBar

**Priority:** P1. **Purpose:** Model-driven strip of commands, toggles and parameter editors.

**Contract sketch:** Items reference command/property IDs with grouping and presentation priority; customize arrangement by stable IDs.

**Dependencies and boundary:** CommandRegistry, PropertyBinding and persistent user customization.

**Completion check:** Menu, bar and palette agree on checked/enabled state; reset restores the documented default order.

<a id="ctl-06"></a>
## CTL-06 — OverflowToolBar

**Priority:** P1. **Purpose:** Toolbar with deterministic overflow into an accessible menu.

**Contract sketch:** Measure groups, minimum representations and priority; move complete groups or commands into overflow while retaining IDs.

**Dependencies and boundary:** Existing ToolBar visuals plus command metadata; no hidden duplicate shortcut handlers.

**Completion check:** Resize repeatedly and verify every command is reachable once with stable focus and equivalent enabled state.

<a id="ctl-07"></a>
## CTL-07 — DocumentTabs

**Priority:** P1. **Purpose:** Tabs aware of document lifecycle and detachable views.

**Contract sketch:** Bind sessions/view IDs, dirty/read-only/loading state, close/closeOthers and reorder requests; support overflow and duplicate views.

**Dependencies and boundary:** DocumentManager and WindowHost; ordinary TabStrip remains a simpler selector.

**Completion check:** Close a dirty tab, cancel the prompt, and preserve selection/view state; reorder does not change identity.

<a id="ctl-08"></a>
## CTL-08 — NavigationRail

**Priority:** P1. **Purpose:** Persistent application navigation with groups and badges.

**Contract sketch:** Model entries carry IDs, icons, labels, destinations and permissions; compact/expanded presentation and current destination share state.

**Dependencies and boundary:** Navigation state owned by the app; commands expose activation.

**Completion check:** Keyboard traversal, hidden destinations and route restoration preserve a valid current destination.

<a id="ctl-09"></a>
## CTL-09 — BreadcrumbBar

**Priority:** P1. **Purpose:** Hierarchical navigation through files, objects, nodes or document sections.

**Contract sketch:** Path is a list of stable IDs/labels with optional sibling providers; emit navigateRequested and support overflow.

**Dependencies and boundary:** Hierarchy provider and asynchronous loading contracts.

**Completion check:** Deep paths remain usable in a narrow panel and resolve navigation by ID rather than displayed text.

<a id="ctl-10"></a>
## CTL-10 — ShortcutEditor

**Priority:** P1. **Purpose:** Record, search and resolve command bindings.

**Contract sketch:** Filter commands, capture sequences/chords, show conflicts/scopes and restore defaults; staged edits validate before applying.

**Dependencies and boundary:** ShortcutMap and CommandRegistry.

**Completion check:** Conflicts across contexts are explained; recording Escape/Tab follows explicit capture rules and never traps focus.

<a id="ctl-11"></a>
## CTL-11 — PreferencesDialog

**Priority:** P1. **Purpose:** Searchable staged editor for settings categories.

**Contract sketch:** Read SettingsModel schema; expose search, defaults, apply/cancel and restart-needed state with per-field validation.

**Dependencies and boundary:** SettingsModel and common form editors.

**Completion check:** Search finds nested settings, cancel discards staged changes, and keyboard focus reaches validation failures.

<a id="ctl-12"></a>
## CTL-12 — Inspector

**Priority:** P1. **Purpose:** Schema-driven property panel for single or multiple selection.

**Contract sketch:** Property descriptors select editors, groups, units, mixed/reset/locked/animated state; filtering and pinned properties are optional.

**Dependencies and boundary:** PropertyBinding, SelectionModel and typed editor registry.

**Completion check:** A mixed selection displays mixed values accurately and commits one undoable multi-target edit.

<a id="ctl-13"></a>
## CTL-13 — MixedValueField

**Priority:** P1. **Purpose:** Shared mixed/unset/invalid/locked presentation for property editors.

**Contract sketch:** Wrap an editor with state, reset affordance and validation message; preserve distinction between no value, mixed values and zero.

**Dependencies and boundary:** PropertyBinding and semantic type formatter.

**Completion check:** Typing into a mixed field commits a value deliberately; merely focusing it never replaces mixed values with a default.

<a id="ctl-14"></a>
## CTL-14 — UnitField

**Priority:** P1. **Purpose:** Numeric entry with physical or domain units.

**Contract sketch:** Value in canonical units plus displayUnit, allowed units, precision and parser; emit transaction-aware edits after conversion.

**Dependencies and boundary:** Unit registry; extend NumberField interaction rather than duplicate it.

**Completion check:** Switch mm/inches or seconds/samples without altering canonical value beyond documented rounding.

<a id="ctl-15"></a>
## CTL-15 — ExpressionField

**Priority:** P2. **Purpose:** Numeric entry supporting bounded expressions and references.

**Contract sketch:** Text, evaluated value, errors and permitted symbol scope; preview and commit explicit expression/value forms.

**Dependencies and boundary:** Restricted parser/evaluator supplied by the host; no arbitrary code execution implicit in typing.

**Completion check:** Invalid/divide-by-zero/cyclic input produces an error and preserves the last committed value.

<a id="ctl-16"></a>
## CTL-16 — VectorField

**Priority:** P1. **Purpose:** Linked numeric components for positions, sizes and directions.

**Contract sketch:** Component labels/units, value vector, lock modes and mixed states; per-axis preview plus one combined commit.

**Dependencies and boundary:** PropertyBinding and UnitField; domain defines dimensionality and constraints.

**Completion check:** Ratio-locked edits respect limits and undo as one edit without incremental rounding drift.

<a id="ctl-17"></a>
## CTL-17 — TransformEditor

**Priority:** P1. **Purpose:** Inspector for translation, rotation, scale, pivot and bounds.

**Contract sketch:** Expose canonical transform and decomposition policy; bind canvas handles and numeric edits to the same transaction.

**Dependencies and boundary:** Canvas transform adapter and domain geometry model.

**Completion check:** Handle and numeric edits agree, including rotated/scaled objects and cancel/reset behavior.

<a id="ctl-18"></a>
## CTL-18 — FontPicker

**Priority:** P1. **Purpose:** Searchable font family/style chooser with specimen preview.

**Contract sketch:** Font model includes family/style/coverage/missing state; lazy preview and variable-axis capability; emit fontSelectionRequested.

**Dependencies and boundary:** Font discovery/licensing/availability adapter and document formatting model.

**Completion check:** Missing fonts remain named; filtering large font lists stays responsive and fallback is explicit.

<a id="ctl-19"></a>
## CTL-19 — FontFeaturesEditor

**Priority:** P2. **Purpose:** Advanced typography controls for OpenType and variable fonts.

**Contract sketch:** Expose supported axes/features, ranges and paragraph/character formatting targets; unsupported settings remain inspectable.

**Dependencies and boundary:** Text shaping/layout engine reports actual capabilities.

**Completion check:** Changing an axis or feature yields a reversible edit and reports unsupported glyph/font combinations.

<a id="ctl-20"></a>
## CTL-20 — FilePathField

**Priority:** P1. **Purpose:** Location editor with browse, validation and recent paths.

**Contract sketch:** URI/display path, open/save/folder mode, filters and validation; emit browse/open requests via platform services.

**Dependencies and boundary:** FileDialogAdapter, resource resolver and storage policy.

**Completion check:** Network/virtual URIs remain intact; a canceled browse leaves the field unchanged.

<a id="ctl-21"></a>
## CTL-21 — TokenField

**Priority:** P1. **Purpose:** Multi-value input with chips, completion and invalid-token states.

**Contract sketch:** Model stable token IDs and labels; add/remove/reorder with completion provider; keyboard selection and text entry coexist.

**Dependencies and boundary:** Validation and optional async lookup for recipients/tags/filters.

**Completion check:** Backspace/undo and paste-many work with IME; late completion cannot replace a committed token.

<a id="ctl-22"></a>
## CTL-22 — DateTimeField

**Priority:** P2. **Purpose:** Locale-aware date/time editor with explicit zone and precision.

**Contract sketch:** Canonical instant or local date/time, display zone, valid range and ambiguity state; parse/commit/clear follow field semantics.

**Dependencies and boundary:** Calendar/time-zone service; all-day dates remain distinct from instants.

**Completion check:** Daylight-saving gaps/overlaps are explained; locale display changes preserve the intended time.

<a id="ctl-23"></a>
## CTL-23 — CalendarPicker

**Priority:** P2. **Purpose:** Accessible date or date-range chooser.

**Contract sketch:** Selected date/range, first weekday, locale, bounds and marked dates; keyboard month/year navigation and selection requests.

**Dependencies and boundary:** Calendar model with a stated calendar system and DateTimeField.

**Completion check:** Cross-month selection and leap-day limits work by keyboard with clear accessible labels.

<a id="ctl-24"></a>
## CTL-24 — FormGroup

**Priority:** P1. **Purpose:** Consistent labeled form layout with validation summary.

**Contract sketch:** Fields expose labels, required/optional/help/error state and commit policy; group can find first invalid field.

**Dependencies and boundary:** Existing PropertyRow/Grid plus ValidationModel.

**Completion check:** Labels remain associated with inputs after responsive reflow; async pending differs from invalid.

<a id="ctl-25"></a>
## CTL-25 — FindReplaceBar

**Priority:** P1. **Purpose:** Reusable search/replace control with target and options.

**Contract sketch:** query/replacement, case/whole-word/pattern/scope, match count/index, next/previous/replace/all; results are revision-tagged.

**Dependencies and boundary:** Document-specific search adapter and transactions for replacements.

**Completion check:** Replacement respects selection scope, supports cancellation and undoes a replace-all as a deliberate group.

<a id="ctl-26"></a>
## CTL-26 — NotificationCenter

**Priority:** P1. **Purpose:** Persistent list of actionable application notices.

**Contract sketch:** Items have stable ID, severity, source, timestamp, read state and command actions; aggregation prevents repetitive floods.

**Dependencies and boundary:** Notification service and CommandRegistry; domain decides severity.

**Completion check:** Repeated failures aggregate; dismiss/read state and actions remain consistent across windows.

<a id="ctl-27"></a>
## CTL-27 — Toast

**Priority:** P1. **Purpose:** Transient announcement with optional action and accessible lifetime.

**Contract sketch:** Message, priority, duration policy and action ID; pause while hovered/focused and respect reduced motion.

**Dependencies and boundary:** NotificationCenter queue and accessible live announcements.

**Completion check:** A keyboard user can reach the action before expiry; important failures also remain in notification history.

<a id="ctl-28"></a>
## CTL-28 — TaskCenter

**Priority:** P1. **Purpose:** User-facing monitor for background operations.

**Contract sketch:** Read JobModel with state/progress/dependencies, per-job details, cancellation/retry and result opening.

**Dependencies and boundary:** JobModel and command routing; workers retain execution ownership.

**Completion check:** Failed/canceled/running jobs display distinct states; retry creates traceable work without duplicate outputs.
