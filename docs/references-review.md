<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Equations, references and document review

WP-052 adds three kinds of semantic content to the [rich-text model](rich-text.md):
equations, reference fields (captions, cross-references, citations and a
bibliography) and review (comment threads and tracked changes). All three
are part of the document itself. Equations are inline objects, fields are
runs of generated text, and review ranges are marks in character formats.
They take part in layout, pagination, PDF output, copy and paste, undo and
persistence like any other text. `EquationEditor`, `ReferenceManager` and
`ReviewPane` only send requests. [examples/document-review](../examples/document-review/Main.qml)
binds all three to one `RichTextDocument`. Decisions:
[D-114](decisions-domains.md#d-114--equations-are-inline-objects-with-a-tex-subset-source),
[D-115](decisions-domains.md#d-115--field-results-are-rewritten-inside-the-committing-transaction),
[D-116](decisions-domains.md#d-116--review-ranges-are-character-format-marks) and
[D-117](decisions-domains.md#d-117--annotations-are-richtext-version-2-members).

| Part | Files |
| --- | --- |
| Records and result-text rules | `src/core/rich_text_review.{h,cpp}` |
| Equations | `rich_text_equation.{h,cpp}` (parser), `rich_text_equation_text.cpp` (canonical source, linear text, palette), `rich_text_equation_layout.cpp` (box layout, painting), `rich_text_equation_edit.cpp` (document operations) |
| Reference fields | `rich_text_fields.cpp`, `rich_text_fields_p.h`, `rich_text_fields_snapshots.cpp` |
| Review | `rich_text_review_edit.cpp`, `rich_text_review_snapshots.cpp`; tracking in `rich_text_ops.cpp` and `rich_text_fragment.cpp` |
| Persistence | `rich_text_codec_review.cpp`, `rich_text_codec*.cpp`, `rich_text_session_adapter.cpp` |
| View | `rich_text_painter.cpp` (review colours, equation painting), `rich_text_shaping.cpp`, `rich_text_area_structure.cpp` (requests at the selection) |

## Equations

An equation is an inline `RichObject` of kind `equation`. Its `source` is
the canonical source in the TeX subset below, and its `altText` is the
linear text. Its width and height come from the equation layout at the
document's default size. It sits on the baseline, takes its width in the
line, and paints itself from the source on every page, in every view and in
PDF output. Other object kinds still go to the host's object renderer.

**Supported subset.** Anything else is refused as `unsupported`, and a
syntax error as `invalidArgument`. Both errors give the UTF-16 offset in the
source.

| Construct | Source |
| --- | --- |
| Variables and numbers | letters (italic), `3.14` |
| Operators | `+ - = < > , ; : ! ' \| / * ( ) [ ] . ?`, any Unicode math symbol, `\pm \mp \times \cdot \div \ast \circ \leq \le \geq \ge \neq \ne \approx \equiv \sim \simeq \propto \ll \gg \to \rightarrow \leftarrow \Rightarrow \Leftarrow \Leftrightarrow \mapsto \in \notin \subset \supset \subseteq \supseteq \cup \cap \forall \exists \neg \wedge \vee \ldots \cdots \prime \perp \parallel \angle \mid \{ \} \|` |
| Greek and symbols | `\alpha` … `\omega` (with `\varepsilon \vartheta \varpi \varrho \varsigma \varphi`), `\Gamma \Delta \Theta \Lambda \Xi \Pi \Sigma \Upsilon \Phi \Psi \Omega`, `\infty \partial \nabla \ell \hbar \emptyset` |
| Groups and scripts | `{…}`, `x^2`, `x_i`, `x_i^2` |
| Fractions and roots | `\frac{a}{b}`, `\sqrt{x}`, `\sqrt[3]{x}` |
| Large operators | `\sum \prod \coprod \bigcup \bigcap` (limits above and below), `\int \iint \oint` (limits at the side) |
| Functions | `\sin \cos \tan \cot \sec \csc \arcsin \arccos \arctan \sinh \cosh \tanh \log \ln \lg \exp \det \dim \min \max \sup \inf \lim \gcd \deg \arg \ker \Pr` |
| Fences | `\left( … \right)` with `( ) [ ] \| . \{ \} \langle \rangle \\|` |
| Text and spacing | `\text{…}`, `\mathrm{…}`, `\, \: \; \! \quad \qquad` |

Environments (`\begin`), `\over`, `&`, `\\`, font commands (`\mathbb`,
`\mathbf`) and accents (`\hat`, `\vec`) are outside the subset. A source is at
most 4,096 characters and nests at most 32 groups deep.

`Equation::parse` gives an AST (`Node`). `canonicalSource` writes it back,
so that `parse(canonicalSource(n)) == n`. `linearText` gives the readable
alternative, for example `(a + b)/2`, `∑_(i = 1)^n i` or `√(x^2 + 1)`.
Layout follows TeX's proportions. Scripts are 70 % of the size, large
operators 140 %, and fractions and large operators sit on the math axis at a
quarter em. Radicals are drawn strokes and fences stretch to their content.
Glyphs use `STIX Two Math`, `Latin Modern Math`, `DejaVu Serif` or `serif`,
in that order.

| Method (QML) | Effect |
| --- | --- |
| `equations`, `equationSnapshot(id)` | `{id, source, linearText, status, supported, error, errorCode, errorPosition, location:{paragraphId, offset}, supportedSymbols, widthPt, heightPt}`, the shape `EquationEditor.equation` takes. |
| `equationSymbols` | Palette `[{id, label, source, tooltip}]` for `EquationEditor.symbols`. |
| `validateEquation(source)` | `{valid, code, message, position, canonical, linearText}`, for live validation. |
| `insertEquation(paragraphId, offset, source)` | Returns the new object ID, or "" with `lastError` and `lastErrorCode`. `RichTextArea.insertEquation(source)` inserts it at the caret, replacing the selection. |
| `setEquationSource(id, source)` | One `StepObject`: the new source and size together. Undo restores both. |
| `equationSourceWithSymbol(source, cursor, symbolId)` | The source with a palette entry inserted. It adds a space where a command would run into a letter. |

## Reference fields

A field is a run of characters whose format names a `ReferenceField`
(`CharFormat.fieldId`). The text of the run is the field's result.

| Type (QML) | Kind | Target | Result |
| --- | --- | --- | --- |
| `caption` | sequence | a series name (`sequence:Figure`, or `Figure`) | `Figure 2`; `(2)` for `Equation`. Numbered per series in document order. |
| `cross-reference` | crossReference | a caption field | the target's result, for example `Figure 2` |
| `citation` | citation | a source | `[n]`, where n is the source's rank by first citation in document order |
| `bibliography` | bibliography | none | `[n] Author. Title. Publisher, Year.` for each cited source (a part that already ends in a period, such as `Wu, L.`, gets no second one), joined by soft line breaks, or `No sources cited.` |

### Result text

A field's result is recomputed in every transaction that commits
(`RichTextDocument::reconcileFields`). A stale result is replaced by a
`StepText` in the same transaction. Renumbering is therefore part of the
edit that caused it. It undoes and redoes with that edit, and every view
re-lays only the changed paragraphs.

- **Broken targets.** A cross-reference whose caption was deleted, or
  removed from the text, shows `[?]`. So does a citation whose source was
  removed. The reference manager reports it as `status: "broken"` with an
  `error`. A reference never falls back to another caption, and undo
  restores the original target.
- **Atomic results.** Typing inside a result goes after it, and typed text
  never joins a field. Deleting any part of a result deletes all of it.
  Pasting inside a result pastes after it. A result keeps the look of its
  first character.
- **Stale results.** A result edited outside the toolkit reports
  `status: "stale"`. `updateFields()` rewrites every stale result as one
  undo step.

| Method (QML) | Effect |
| --- | --- |
| `references` | `[{id, type, label, targetId, sequence, number, status, formattedText, expectedText, error, location:{paragraphId, offset, length}}]` for placed fields in document order. This is the shape `ReferenceManager.references` takes. `status` is `resolved`, `broken` or `stale`. |
| `referenceTargets` | Captions `{id, type:"caption", label, location}`, sources `{id, type:"source", label}` and series `{id:"sequence:<name>", type:"sequence", label}`, for `ReferenceManager.targets`. |
| `referenceTypes` | `caption`, `cross-reference`, `citation`, `bibliography`, for `ReferenceManager.supportedTypes`. |
| `insertReference(paragraphId, offset, type, targetId)` | Returns the field ID. `RichTextArea.insertReference(type, targetId)` inserts at the caret and leaves the caret after the result. A `footnote` is refused as `unsupported` (see [scope](#scope)). A target that does not fit the type is refused as `invalidArgument`. |
| `editReference(id, {label, type, targetId})` | Relabel, retarget or change type (a bibliography keeps its type). One undo step. |
| `removeReference(id)` | Deletes the result text. Under tracking, the text is marked deleted. |
| `sources`, `addSource(map)`, `updateSource(id, patch)`, `removeSource(id)` | Bibliography sources `{id, author, title, year, publisher, label, number, cited}`. A source needs an author or a title. |
| `updateFields()` | Rewrites stale results. |

## Review

### Marks

Review ranges are character-format marks: `commentIds` (sorted), and one
`insertionId` and one `deletionId` per character. Because marks travel with
the characters, edits before a range move it, edits inside it resize it,
and ranges may overlap. Undo restores ranges exactly because it restores
the spans. Typed text joins a comment only strictly inside it, and never
takes a change mark from its neighbours.

### Comments

`addComment(startParagraphId, startOffset, endParagraphId, endOffset, body)`
marks a non-empty range and creates a `CommentThread` `{id, author, time, body,
resolved, replies}`. `RichTextArea.addComment(body)` uses the selection.
`replyToComment`, `resolveComment(id, resolved)` and `removeComment` are one
undo step each. A thread whose text is deleted becomes `orphaned`, and its
record stays. Undo anchors it again, and so does a paste of the cut text
([moving annotated text](#moving-annotated-text)).

### Tracked changes

With `trackChanges` on, edits are recorded under `reviewAuthor`:

- Typed or pasted text, inserted objects and fields carry an insertion
  change. Typing next to one's own insertion continues it.
- Deletion marks the text instead of removing it. It extends one's own
  adjacent deletion, and it removes one's own tracked insertion outright.
  Paragraphs are not joined, because paragraph marks are not tracked.
- Delete moves the caret past deleted text. Backspace over deleted text only
  moves the caret.
- `TableEditor` cell replacements are tracked too.

`acceptChange(id)` and `rejectChange(id)` are one undo step each. Accepting a
deletion or rejecting an insertion removes the text; the other two keep the
text and drop the mark. When removed text held another reviewer's change,
that change has nothing left to review and its record goes. Their changes
elsewhere keep their text and marks.

| Method (QML) | Effect |
| --- | --- |
| `reviewItems` | Comments and changes in document order, orphans last. Each is `{id, kind:"comment"\|"change", changeKind, author, time, summary, body, quote, status, resolved, threadId, replies, location:{paragraphId, offset, endParagraphId, endOffset}, canAccept, canReject, error}`, the shape `ReviewPane.items` takes. `status` is `open`, `resolved` or `orphaned`. |
| `trackChanges`, `reviewAuthor` | Document-wide tracking switch and the author of new comments, replies and changes. |
| `reviewContextAt(paragraphId, offset)` | For example `Inserted by Ada, Comment by Bob: …`. `RichTextArea.caretContext` includes it, so assistive users hear what is under the caret. |
| `RichTextArea.selectionRange()`, `selectRange(...)` | The selection as paragraph positions; `ReviewPane` navigation selects an item's range. |

The views draw tracked insertions underlined in `insertionColor` (theme
success) and tracked deletions struck through in `deletionColor` (theme
danger). Text under an open comment gets a `commentColor` (theme
`accentSubtle`) background, and resolved comments are not highlighted. Shaping
applies the underline and the strike, so PDF output shows them too.

## Moving annotated text

A paste checks each mark against the destination document:

- A field or comment whose text is no longer anywhere in the document (it
  was cut) is anchored again by the pasted text. A cut and paste therefore
  moves a caption, and its cross-references follow the new number.
- A copy of text that is still in place pastes as plain text, so a field or
  comment is never in two places.
- Change marks never travel. A paste under tracking is an insertion.

If a field's run is ever split, only its first run stays the field.

## Persistence

These members are part of `qindatk.richtext` version 2, and each array is
written only when it is non-empty (D-117):

```json
{ "version": 2,
  "formats": [ {}, { "field": "f1" }, { "comments": ["c1"], "insertion": "i1" }, { "deletion": "d1" } ],
  "fields": [ { "id": "f1", "kind": "sequence", "sequence": "Figure" },
              { "id": "f2", "kind": "crossReference", "target": "f1", "label": "Results" } ],
  "sources": [ { "id": "s1", "author": "Wu, L.", "title": "Field calibration", "year": "2024" } ],
  "comments": [ { "id": "c1", "author": "Ada", "time": "2026-09-24T10:00:00Z", "body": "Why?",
                  "replies": [ { "id": "r1", "author": "Bob", "time": "…", "body": "Because." } ] } ],
  "changes": [ { "id": "i1", "kind": "insertion", "author": "Ada", "time": "…" } ] }
```

- Records are sorted by ID. Every record is written, including orphaned
  comments and fields whose text was cut, so identity survives a save.
- A format mark naming no record of its kind is dropped with a warning, in
  the same way as an unknown character style (a session's live encoding
  keeps unused formats). An invalid record fails with its path, for example
  `fields[0].kind`.
- A cross-reference or citation whose target is missing is valid and reads
  as broken.
- `RichTextCodec::requiredVersion` is 2 when the document has any record.
  Equations alone need no new version: an older reader shows them as
  labelled objects with their linear text.
- Fragments carry marks; the pasting document applies the rules above.

## Controls

- **ReferenceManager.** Bind `references`, `targets` and `supportedTypes` to
  `references`, `referenceTargets` and `referenceTypes`. Route
  `addReferenceRequested` to `RichTextArea.insertReference`,
  `editReferenceRequested` to `editReference`, and `removeReferenceRequested`
  to `removeReference`. For `navigateRequested`, go to the target's or the
  reference's `location`.
- **ReviewPane.** Bind `items` to `reviewItems`. Route accept, reject, reply
  and resolve to the matching methods. For `navigateRequested`, select the
  item's `location`.
- **EquationEditor.** Bind `equation` to `equationSnapshot(id)`, re-read on
  `revision`, and bind `symbols` to `equationSymbols`. For live validation,
  set `errorText` from `validateEquation(sourceDraft)`. Route
  `sourceChangeRequested` to `setEquationSource` and `symbolInsertRequested`
  to `equationSourceWithSymbol`.

## Scope

- **Footnotes and endnotes.** These need a note area at the foot of each
  page in pagination. They are refused as `unsupported`. No package in the
  plan owns them yet (see the WP-052 evidence).
- **Tracked formatting changes and tracked paragraph marks.** These are not
  modelled: tracking covers inserted and deleted characters, objects and
  fields.
- **Qualification work.** Equation output fidelity against a reference
  renderer, screen-reader runs, translation and performance measurement
  belong to WP-088 to WP-094.

## Verification

`tst_rich_text_equations` covers the subset, its typed errors with offsets,
canonical round trips, linear text, known layout relations (fraction, scripts,
radical, limits), painting, and equations as undoable, persisted inline
objects that the page painter draws itself. `tst_rich_text_references`
covers renumbering in one undo step, broken targets, moving a caption by cut
and paste, plain copies, atomic results, citations and the bibliography,
typed refusals, persistence, stale results and dropped marks.
`tst_rich_text_review` covers comment ranges through edits and overlaps,
orphaned comments that recover, replies, resolving, tracked insertions and
deletions with accept and reject undo, another reviewer's content,
persistence, and the editing surface (Delete, Backspace and requests at the
selection). `tst_review_example.qml` drives the example.
