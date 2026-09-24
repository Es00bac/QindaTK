<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-082 interprocess property transport slice

Branch `work/wp082-ipc-transport` from integration commit `9cc552e`.

Implemented:

- `PropertySyncSocketServer` routes bounded typed operations through an
  owner-only `QLocalServer` endpoint. It authenticates a host-granted actor
  credential and checks editor/viewer permission for every submitted edit.
- `PropertySyncSocketClient` replays the server log into an independent local
  service, then resends only its actor's operations. The client requires a
  persistent local service path. That QSaveFile operation log is the durable
  outgoing queue across process exit; operation UUIDs make resend idempotent.
- The existing operation codec is exposed as two bounded C++ functions and is
  used by both the local store and the socket protocol. Presence remains
  ephemeral and does not enter either durable store.
- The installed CMake target now carries the Qt Network dependency.

Focused proof:

- `cmake --build build/wp082-ipc --target tst_property_sync_socket -- -j24 -l24`
  builds the socket adapter and test with the configured direct-build limits.
- `ctest --test-dir build/wp082-ipc -R '^tst_property_sync_socket$'
  --output-on-failure` passes 1/1. The test starts a separate process, makes
  an offline edit, exits it, restarts it from the local durable log, and sees
  the live parent replica update through the socket. It also confirms a
  wrong credential is rejected and a viewer's submitted edit is rejected
  without reaching the server document/log.

Remaining WP-082: remote-network transport, secret provisioning/rotation,
cross-process presence and expiry, domain-specific structural operations,
version browser, branch/restore, domain-aware undo and conflict UI, log
compaction and migrations. A rejected local offline edit remains in that
client's local store; the host must discard/reload the replica or provide a
permission-aware editor before reconnecting it. This slice is not full
WP-082 acceptance.
