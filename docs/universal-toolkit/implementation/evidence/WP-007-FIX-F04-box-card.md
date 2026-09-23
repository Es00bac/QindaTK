<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 F04 — Box and Card actions

**Scope:** make interactive `Tk.Box` and `Tk.Card` keyboard and assistive
technology actions match their existing pointer activation while preserving
passive layout behavior.

## Contract

- `interactive: true` gives the Box a Button role, a Tab stop, and a theme
  focus ring. The inherited Card keeps its existing surface, selected border,
  padding, and hover/pressed tints.
- `tooltip` supplies the accessible name and visible hover tooltip; Box/Card
  provide readable fallback names for interactive instances without a tooltip.
- Pointer click, Space, Return, Enter, and `Accessible.pressAction` share
  `Box.activate()`, which gates on both `interactive` and `enabled` and emits
  `clicked` once for an accepted operation. Right-click and double-click
  remain separate pointer signals and are also disabled-gated.
- Passive Box/Card instances expose `Accessible.NoRole`, are ignored by the
  accessibility tree, and cannot receive Tab focus. Disabled instances lose
  their tab stop and focus ring and reject activation.
- The `boxFocusRing` name is stable. Focus-ring geometry is theme-derived and
  is separate from `contentItem`, preserving child seating and implicit size.

## Verification

`tests/qml/tst_box_card_accessibility.qml` covers the shared actions for Box
and Card, accessible names, the passive case, focus-ring visibility, and the
disabled gate. Preview: `qtk-preview build/wp007-box-card-actions/box_card_preview.qml
--density compact --dump --grab ... --size 440x120`. The opened capture shows
the focus ring on Card and unchanged Box/Card content seating. The relevant
item dump lines were:

```text
Box  12,22 180x72
  Rectangle#boxFocusRing  -2,-2 184x76  hidden
  Item  4,4 172x64
Card  220,18 200x80
  Rectangle#boxFocusRing  -2,-2 204x84
  Item  9,9 182x62
```

The `hidden` suffix on the Box ring confirms a passive-focus state there; the
Card ring has no `hidden` suffix. [Opened focus preview](box-card-focus.png).

## Limits

The automated evidence uses Qt Quick synthetic pointer/key input and the
attached `Accessible.pressAction` signal. It does not qualify native screen
reader delivery, physical keyboard traversal across application windows, or
platform-specific focus rendering.
