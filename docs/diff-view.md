<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# DiffView

`Tk.DiffModel` compares two line snapshots. Call
`setDocuments(leftLines, rightLines, leftRevision, rightRevision)` after the
host captures both documents. Both revision IDs must be nonempty. A change in
content with identical revision IDs is rejected, leaving the prior diff intact.
The model is read-only; it never writes either source.

The built-in local fallback uses deterministic longest-common-subsequence
alignment. It accepts at most 512 lines per side, 2048 UTF-16 code units per
line, and 128 hunks. Time and memory are O(left lines × right lines), capped by
those limits. An excessive input or hunk count returns `false` and sets `error`
without replacing the visible model. Empty and repeated lines are supported.
Changed rows align one left and right line where present; insertion and deletion sides
have line number zero. `get(row)` exposes `kind`, `leftText`, `rightText`,
`leftLine`, `rightLine`, `hunkId`, and `conflictId`. `leftValue` and `rightValue`
carry typed structured values for custom delegates, falling back to text for
local diffs. `hunks` exposes each ID, first row, and both source spans for local
diffs. The local two-way diff uses its `hunkId` as `conflictId`. A page
provider can supply a separate stable conflict ID for a three-way comparison.

For large or structured comparisons, subclass the typed `DiffPageProvider` and
call `setPageProvider(provider, leftRevision, rightRevision)`. Its indexed
`regionCount`/`regionAt` and `hunkCount`/`hunkAt` provide rows on demand;
`replacementFor` supplies a requested hunk's transaction payload. The model
does not copy or truncate provider rows, and `DiffView` uses a virtual ListView.
The host retains the provider for an immutable revision snapshot. Replacing a
provider requires new revision IDs; destroying it clears the view and cancels
pending requests. `hunkAt(index)` is the scalable navigation API; the `hunks`
list is only populated for the built-in local fallback. The 20,000-region test
checks that one requested region causes one provider read and fewer than 50
visible region delegates. The host provider owns its alignment algorithm and
must keep hunk and conflict IDs stable for unchanged logical regions.

Hunk IDs hash changed content and adjacent unchanged lines, rather than line
indices. Accepting a separate hunk and recomputing preserves unaffected IDs
when their changed content and adjacent anchors remain the same. Identical
content with identical anchors gets a deterministic occurrence suffix. Such a
duplicate can renumber if an earlier identical occurrence is removed; hosts
should reconcile with `hunkAt(index)` after every revision.

## Host transaction boundary

Subclass `QindaTK::DiffTransactionAdapter` and set it as the model's
`transactionAdapter`. `requestDecision(hunkId, "accept"|"reject",
expectedLeftRevision, expectedRightRevision)` validates both revisions and
passes a typed `DiffDecision` to `submitDecision`. The decision includes source
span, replacement lines, IDs and both expected revisions. `accept` asks the
host to replace that left span with right lines; `reject` requests retention of
left lines. Returning `true` means queued, not committed. The adapter owns a
reversible document transaction and must check revisions at commit time.
`cancelDecision(requestId)` asks the adapter to cancel queued work. On a new
snapshot or adapter change, the model cancels all outstanding requests. The
host publishes committed snapshots with new revisions via `setDocuments`.
Canceled or rejected requests leave the originals unchanged.

## View

`Tk.DiffView` has `diffModel`, `sideBySide` (default true), `tooltip`,
`currentHunkId`, `leftDelegate`, and `rightDelegate`. Its virtual `diffRows`
list paints aligned left/right cells, or stacked changed lines in inline mode.
The delegates can render text or structured content; they receive
`parent.value`, `parent.row`, and `parent.hunkId`. Up/Down or the Previous/Next
buttons navigate hunks. Clicking a region selects it. Accept and Reject call
the model and emit `decisionRequested` only if the adapter queues the request.
The view does not imply commit success. Stable objectNames are `diffView`,
`diffToolbar`, `diffRows`, `diffRegion`, `diffLeftCell`, `diffRightCell`, and
`diffError`. The toolbar buttons have accessible names and tooltips; each
visible region has an accessible line summary.
