<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-082 first slice: local property synchronization

This evidence covers the bounded local property backend only. It does not
close WP-082; see [remaining scope](../../../collaboration.md#remaining-wp-082-scope).

## Reproducible fixture

`tests/cpp/tst_local_property_sync.cpp` creates one typed baseline reference
document and decodes it into separate `PropertyDocument` instances. Clients A
and B have distinct actor UUIDs and attach to one service with an atomically
persisted operation log. The test starts both online, then disconnects both.
A changes `gain` to 2 and `name` to `offline A`; B concurrently changes
`gain` to 3. Both conflicting `gain` packets have logical time 2. The stable
actor UUID of B ranks higher, so the expected final state on both replicas
is `gain = 3`, `name = offline A`, independent of arrival order. A receives
one `operationLost` signal and all four accepted operations remain in the log.

A third independent replica receives the log in reverse order and a duplicate
packet before attaching. It converges to the same values and replay during
attachment changes nothing. A new service opens the saved log and a fresh
baseline replica replays it after restart. The second test rejects a value
with the wrong Qt type and stops after an external entity mutation, without
adding operations to the service. A third test confirms that applying a
synchronized value invalidates a pre-existing linear `UndoService` step.

## Verification

Run:

```sh
cmake -S . -B build/wp082 -G Ninja -DCMAKE_BUILD_TYPE=Debug -DQINDATK_BUILD_EXAMPLES=OFF -DQINDATK_BUILD_TOOLS=OFF
ninja -C build/wp082 -j24 -l24 tst_local_property_sync tst_document_session tst_edit_transaction
ctest --test-dir build/wp082 -R '^tst_(local_property_sync|document_session|edit_transaction)$' --output-on-failure
```

The direct Ninja limits match `portageq envvar MAKEOPTS` on this machine:
`-j24 -l24`. The focused test is the behavioral gate for this slice; the
full WP-082 acceptance criteria still require the remaining work above.

Result on 2026-09-24: all three tests passed under Qt 6.11.1 (3/3).
