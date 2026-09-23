<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 / FIX-F04 — Menu action accessibility and notifications

This focused slice audits `MenuItem`, `MenuBar`, and their nested menu action
paths. It does not claim full WP-007 or native screen-reader qualification.

## Findings and repair

Before the repair, a Qt QML assertion showed that `MenuItem.Accessible.name`
was empty even though the control displayed its text. A checked-state probe
showed `Accessible.checked` stayed false after assigning `checked: true`, and
the disabled menu entry remained accessibility-focusable. `Accessible.pressAction`
did not activate a MenuItem or emit its `triggered` signal.

`MenuItem` now declares the MenuItem role and a name derived from its text,
using the same `&` mnemonic display rule as its visual label. Its accessible
checked state follows `checked`, disabled entries are not focusable, and an
accessible press calls `click()` only while enabled. Qt template keyboard
activation and the item/bar mnemonic paths already worked, so those paths
remain template-owned. `MenuBar` and its entries now declare explicit roles
and readable names.

## Verification

`tests/qml/tst_menu_actions_accessibility.qml` verifies:

- MenuItem name/role, checked state after owner assignment, and silent owner
  assignment (`triggered` remains zero).
- Space and accessible press each toggle a checkable item and emit one
  `triggered` signal.
- A disabled item is not accessibility-focusable and cannot be pressed;
  keyboard navigation skips it and Return activates the next enabled action.
- A normal item action through Return emits once.
- Alt+F opens the File menu and Alt+N activates its New action. MenuBar and
  its entry expose the expected roles and mnemonic-clean names.

Configure/build and focused test commands:

```text
cmake -S . -B build/menu-actions -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build/menu-actions
ctest --test-dir build/menu-actions -R '^tst_qml$' --output-on-failure
```

The full QML aggregate passed on Qt 6.11.1. The existing popup-focus fixture
also passed, including nested submenu navigation and Escape focus return.
Native accessibility backend delivery remains untested.
