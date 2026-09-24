<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-033 installed document workflow consumer

This standalone consumer links only the installed `QindaTK::qindatk` target.
It creates two reference documents with stable entity IDs and independent
saved baselines. One command registry routes edits through the active document
scope; an intentionally stale revision is rejected. A shared clipboard copies
text from document A into document B through a command, and the manager's one
undo facade keeps the two histories independent.

The consumer captures a two-document workspace, saves it, then loads it into a
fresh manager and verifies saved content, active document and stable view IDs.
It closes restored document B while a snapshot-bound worker is blocked, then
releases the worker and checks its stale result stays unpublished and active
document A retains its value.

An external replacement of A's saved file then makes Save All fail for A while
B saves successfully. The fixture reopens B from disk and restores A's latest
edit from its recovery journal without replacing the external file. All files,
clipboard data and recovery entries live in a temporary directory or memory.

Configure with the matching Qt SDK and installed QindaTK stage in
`CMAKE_PREFIX_PATH`, then run `cmake --build <build-dir>` and the resulting
`qindatk_wp033_consumer` with `QT_QPA_PLATFORM=offscreen`. The fixture checks
the reference `PropertyDocument` path. It does not cover domain serializers,
cross-document atomic moves, worker save cancellation, crash durability, or
cross-document rich-object transfer.
