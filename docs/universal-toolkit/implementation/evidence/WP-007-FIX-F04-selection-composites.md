<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Selection composite interaction slice

**Scope:** Segmented, PillSwitcher, and TabStrip keyboard navigation,
focusability, accessible state/actions, disabled entries, and owner-versus-user
updates. This closes the listed selection-composite slice only; it does not
claim whole WP-007 acceptance.

The source audit found three concrete gaps. Segmented's declared `enabled_`
property was disconnected from the inherited `enabled` state, and its PageTab
items did not expose selected state or arrow-key navigation. PillSwitcher had
keyboard selection but did not skip disabled model entries or guard its
activation helper against them. TabStrip put focus on its root while setting
every tab delegate to `NoFocus`, so arrow keys could change selection without
moving item focus; its PageTab role and selected state were also implicit.

The controls now share a model-entry `enabled: false` convention. Disabled
entries cannot receive focus, change selection, or invoke close actions;
keyboard navigation skips them. Segmented keeps `enabled_` as a compatibility
alias to the real inherited `enabled` property. The three composites expose a
named PageTabList and individual PageTab accessible names and selection state.
TabStrip tabs now take focus directly, and focus moves with keyboard selection.
PillSwitcher keeps wraparound arrows and adds Home/End; TabStrip uses bounded
arrows and Home/End. Programmatic currentIndex assignment remains silent in
PillSwitcher and TabStrip; activation signals report user actions.

**Verification:** `cmake --build build/wp007-selection --target tst_qml`
completed. `ctest --test-dir build/wp007-selection -R '^tst_qml$'
--output-on-failure` passed. The focused [selection composite tests](../../../../tests/qml/tst_selection_composites.qml)
cover disabled entries, keyboard focus/selection, accessible activation,
close-action blocking, and programmatic assignment.

Integration image review found the delegate focus rings stayed hidden after
`forceActiveFocus(Qt.TabFocusReason)`. `Segmented`, `PillSwitcher` and
`TabStrip` now show their themed rings when the corresponding delegate has
active focus, with stable objectNames `segmentFocus_<index>`,
`pillSwitcherFocus_<index>` and `tabFocus_<index>`. The focused QML test now
asserts ring visibility. A 520×260 Compact preview dump showed
`TabButton#tab_0 0,0 49x22` with `Rectangle#tabFocus_0 -2,-2 53x26`
visible. A Touch 3× dump showed `TabButton#tab_0 0,0 122x48` and its ring
`-3,-3 128x54`. Both ignored PNGs,
`build/wp007-controls-compact-focus.png` and
`build/wp007-controls-touch-3x.png`, were opened and inspected.
