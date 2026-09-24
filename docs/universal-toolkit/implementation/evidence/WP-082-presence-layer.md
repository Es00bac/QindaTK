<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-082 presence overlay slice

Branch `work/wp082-ipc-transport`, following `df600d6`.

`Tk.PresenceLayer` binds either presence signal source and renders labeled
remote cursors, selections and activity. A host mapper resolves a stable
document anchor into layer coordinates; `viewRevision` remaps after zoom,
scroll or layout changes. Unknown anchors are hidden. Peer state is ephemeral
and bounded. Leave, local disconnect, socket disconnect, client replacement
and stale timeout remove it. The layer contains no input handler or path to
the local document/selection.

Focused proof:

- `cmake --build build/wp082-ipc --target tst_presence_layer qtk-preview --
  -j24 -l24` passed with the configured system build limits.
- `ctest --test-dir build/wp082-ipc -R '^tst_presence_layer$'
  --output-on-failure` passed. It connects two local clients to the same
  service, sends an actual presence packet, checks cursor/selection/activity,
  remaps after a view revision, then checks leave and stale expiry. Both
  document revisions and the durable service log remain unchanged.
- Gallery `--check` passed. The direct section preview reported compact
  `PresenceLayer#presenceLayer 0,0 620x228` and comfortable `0,0 616x228`.
  Both captures were opened and inspected: [compact](WP-082-presence-compact.png)
  and [comfortable](WP-082-presence-comfortable.png). The remote names and
  activities are readable without relying on colour.

Remaining WP-082 scope: the overlay does not rebase anchors after edits or
merge domain text. Host views supply and validate stable anchors; transport
level idle expiry and remote network sessions remain separate work.

After cherry-pick into the shared integration tree, the focused
`tst_presence_layer` check passed **1/1**. Sol reopened the compact capture.
WP-081 remains a declared dependency, so WP-082 stays active.
