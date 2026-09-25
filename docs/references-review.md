<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Equations, references and document review

WP-052 adds three kinds of semantic content to the [rich-text model](rich-text.md):
equations, reference fields (captions, cross-references, citations, a
bibliography, footnotes and endnotes) and review (comment threads and tracked changes). All three
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
| Equations | `rich_text_equation.{h,cpp}` (parser), `rich_text_equation_text.cpp` (canonical source, linear text, palette), `rich_text_equation_layout.cpp` (box layout, painting, editing stops), `rich_text_equation_edit.cpp` (document operations) |
| Visual equation editing | `rich_text_equation_editor.{h,cpp}`, `rich_text_equation_caret.cpp`, `rich_text_equation_editor_p.h` (`Equation::Editor`), `equation_canvas.{h,cpp}` (`Tk.EquationCanvas`) |
| Reference fields | `rich_text_fields.cpp`, `rich_text_fields_p.h`, `rich_text_fields_snapshots.cpp`; notes in `rich_text_notes.cpp` and `rich_text_pagination.cpp` |
| Review | `rich_text_review_edit.cpp`, `rich_text_review_snapshots.cpp`; tracking in `rich_text_ops.cpp`, `rich_text_fragment.cpp`, `rich_text_review_track.cpp` (formatting, paragraph marks, navigation) and `rich_text_review_structure.cpp` (table rows and columns, equations, fields) |
| Persistence | `rich_text_codec_review.cpp`, `rich_text_codec_track.cpp`, `rich_text_codec*.cpp`, `rich_text_session_adapter.cpp` |
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

### Visual editing

`Tk.EquationEditor` shows a visual view (`Tk.EquationCanvas`) above the
source view. The visual view edits the expression tree with
`Equation::Editor`. The editor keeps a normalized tree in which every slot
(numerator, denominator, radicand, root index, script, fence content) is a
row. The caret is a position in one row, and a selection is a range of one
row. After every edit the editor writes the canonical source, and the
editor checks that the source parses back to the same tree
(`normalized(parse(source())) == root()`). An edit that fails this check is
refused and the tree stays as it was. A source edit re-derives the tree, and
the caret stays where its row still exists. The two views therefore
round-trip, and `EquationEditor.sourceDraft` is the single draft that Apply
sends to `setEquationSource`, as one document undo step.

| Input | Effect |
| --- | --- |
| Letters, digits, `+ - = < > , ; : ! \| * ( ) [ ] . ?`, Unicode math symbols, `'` (prime) | Inserted at the caret. Digits join the number before them, and `3.1` reads as one number. |
| `^`, `_` | A superscript or subscript on the item before the caret (a selection is replaced first). The caret goes into the script. |
| `/` | A fraction. The selection, or the term before the caret, becomes the numerator and the caret goes to the denominator. After an operator the fraction starts empty, with the caret in the numerator. |
| Left, Right | Move by one item, and enter or leave a structure's slots in visual order. Shift extends the selection within the row. |
| Up, Down | Move to the slot above or below: numerator and denominator, superscript and subscript, root index and radicand. |
| Home, End | Start or end of the row. Shift extends. |
| Backspace, Delete | Remove the selection or the item next to the caret. At the start of a structure's first slot, Backspace unwraps the structure into its row. |
| Ctrl+A | Selects the whole equation. |
| Click, drag | Put the caret at the nearest stop of the editing layout. Dragging selects. |
| Palette | `insertSymbol(id)` inserts the palette entry at the caret. A selection moves into the entry's first empty slot, and the caret goes to the next empty slot. |

The canvas paints the same `Equation::layout` as the page. Editing layouts also
record caret stops and give an empty slot a placeholder box. Colours, size and
padding come from the control (`Tk.Theme` roles in `EquationEditor`), and
`linearText` is the canvas's accessible description.

## Reference fields

A field is a run of characters whose format names a `ReferenceField`
(`CharFormat.fieldId`). The text of the run is the field's result.

| Type (QML) | Kind | Target | Result |
| --- | --- | --- | --- |
| `caption` | sequence | a series name (`sequence:Figure`, or `Figure`) | `Figure 2`; `(2)` for `Equation`. Numbered per series in document order. |
| `cross-reference` | crossReference | a caption field | the target's result, for example `Figure 2` |
| `citation` | citation | a source | `[n]`, where n is the source's rank by first citation in document order |
| `bibliography` | bibliography | none | `[n] Author. Title. Publisher, Year.` for each cited source (a part that already ends in a period, such as `Wu, L.`, gets no second one), joined by soft line breaks, or `No sources cited.` |
| `footnote` | footnote | the note text (plain, at most 4,000 characters) | the note number `1`, `2`, … in superscript, in document order |
| `endnote` | endnote | the note text | the note number in lower-case roman numerals `i`, `ii`, …, counted separately |

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
| `references` | `[{id, type, label, targetId, noteText, sequence, number, status, formattedText, expectedText, error, location:{paragraphId, offset, length}}]` for placed fields in document order. This is the shape `ReferenceManager.references` takes. `status` is `resolved`, `broken` or `stale`. |
| `referenceTargets` | Captions `{id, type:"caption", label, location}`, sources `{id, type:"source", label}` and series `{id:"sequence:<name>", type:"sequence", label}`, for `ReferenceManager.targets`. |
| `referenceTypes` | `caption`, `cross-reference`, `citation`, `bibliography`, `footnote`, `endnote`, for `ReferenceManager.supportedTypes`. |
| `insertReference(paragraphId, offset, type, targetId)` | Returns the field ID. `RichTextArea.insertReference(type, targetId)` inserts at the caret and leaves the caret after the result. For a note, `targetId` is the note text. A target that does not fit the type is refused as `invalidArgument`. |
| `insertNote(paragraphId, offset, kind, text)`, `setNoteText(id, text)` | The note forms of `insertReference` and `editReference`. Empty text is `invalidArgument`. A footnote in a table cell is `unsupported` (use an endnote). |
| `editReference(id, {label, type, targetId, text})` | Relabel, retarget or change type. A bibliography keeps its type, and a note can only become the other kind of note. `text` replaces a note's text. One undo step. |
| `removeReference(id)` | Deletes the result text. Under tracking, the text is marked deleted. |
| `sources`, `addSource(map)`, `updateSource(id, patch)`, `removeSource(id)` | Bibliography sources `{id, author, title, year, publisher, label, number, cited}`. A source needs an author or a title. |
| `updateFields()` | Rewrites stale results. |

### Notes

A footnote or endnote is a field whose result is its number and whose
record holds its text. Numbering, renumbering, undo, moves and
persistence therefore work as they do for other fields. Pagination places
the text:

- **Footnotes** go at the foot of the page column that holds the
  reference's line. The region shrinks by the note's height
  as its line is placed. The first note on a page has a short rule above
  it, and a line whose note does not fit moves to the next page with its
  note. A footnote's text is its number, a tab and the note text.
- **Endnotes**, and every note in a continuous (unpaged) layout, follow the
  last block, and continue across pages as needed.
- A note whose reference is marked deleted by tracking is not set until the
  deletion is rejected.
- A footnote cannot sit in a table cell, because a table row is laid out
  as a whole. An endnote can.

## Review

### Marks

Review ranges are character-format marks: `commentIds` (sorted), one
`insertionId` and one `deletionId` per character, and a `formatChangeId`
with its `formatSlot` for tracked formatting. A paragraph break carries its
insertion or deletion mark in the `markFormat` of the paragraph before it. Because marks travel with
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

With `trackChanges` on, edits are recorded under `reviewAuthor`. Every
content edit is either recorded or refused with `unsupported` (the refusal
names the operation and says to turn tracking off); nothing applies
untracked.

- **Text.** Typed or pasted text, inserted objects and fields carry an
  insertion change. Typing next to one's own insertion continues it.
- **Deletion** marks the text instead of removing it. It extends one's own
  adjacent deletion, and it removes one's own tracked insertion outright.
  Delete moves the caret past deleted text. Backspace over deleted text only
  moves the caret.
- **Paragraph breaks.** Enter, and a pasted break, mark the new break
  inserted in the same change as the text around it. A deletion across
  paragraphs marks the breaks deleted. Accepting that deletion joins the
  paragraphs, so the result is the same document as the untracked delete.
  Rejecting an inserted break joins its paragraphs again. Deleting one's own
  inserted break joins the paragraphs outright.
- **Formatting.** Character formatting, character styles, paragraph formats,
  paragraph styles, lists and page breaks before a paragraph are recorded
  as a `format` change. Its record keeps each former character format
  (`formats`, at most 1,024 distinct ones per change) and each paragraph's
  former format (`paragraphs`). Accepting drops the marks and the record.
  Rejecting restores the former look. A pending change keeps the look from
  before its first edit, and deleted text or one's own insertion is
  reformatted without a record.
- **Tables.** Cell text replaced in `TableEditor` is tracked as text.
  Deleting a row or column marks its text deleted in a change that names
  the table and the row or column. Accepting it removes the text and then
  the row or column, together with any other reviewer's change that only
  lived there. Rejecting it keeps them. Inserting a table, a row or a
  column, merging or splitting cells, column widths and table styles are
  refused.
- **Equations and fields.** A new equation source leaves the old equation
  marked deleted and inserts the new one, which keeps the equation's ID.
  Retargeting a reference, changing its type, or changing a note's text is
  the old field marked deleted beside a new one. A caption's series cannot
  be retargeted while tracking, because cross-references follow the
  caption's ID. A reference's `label` is panel metadata, not text, and is
  never tracked.
- **Not content.** Page setup, document defaults, text frames, object sizes
  and style definitions are document settings and apply as usual. Moving a
  section is refused.

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
| `RichTextArea.selectionRange()`, `selectRange(...)` | The selection as paragraph positions. |
| `reviewItemLocation(id)` | The item's anchored range `{paragraphId, offset, endParagraphId, endOffset}`, or `{}` for an orphan or an unknown ID. |
| `adjacentReviewItem(paragraphId, offset, forward, kind)` | The next (or previous) anchored item after (or before) a position in document order, wrapping around. `kind` `"comment"` or `"change"` limits it. Orphans are skipped. |
| `RichTextArea.goToReviewItem(id)`, `goToAdjacentReviewItem(forward, kind)` | Select an item's range and scroll to it. The adjacent form steps from the start of the selection and returns the ID it went to. |

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
- A field or comment whose only other text is marked deleted by tracking
  is also anchored again by the paste. A tracked cut and paste is therefore
  a move: accepting the deletion leaves the pasted caption, and its
  cross-references, resolved, and rejecting both changes puts it back.
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

Notes add `"text"` to their field (`{ "id": "n1", "kind": "footnote",
"text": "…" }`). A format mark names a formatting change with
`"formatChange"` and `"formatSlot"`. A `format` change adds `"formats"`
(former character formats, in the same form as the document's `formats`)
and `"paragraphs"` (`[{ "paragraph": id, "format": {…} }]`), and a deleted
row or column adds `"table"` with `"row"` or `"column"`. A `formatSlot`
outside the record's `formats` drops the mark with a warning.

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
  For a `footnote` or `endnote` draft the panel shows a note-text field in
  place of the target, and `addReferenceRequested` passes that text as its
  `targetId`; an edit sends it as `text`.
- **ReviewPane.** Bind `items` to `reviewItems`. Route accept, reject, reply
  and resolve to the matching methods. For `navigateRequested`, call
  `RichTextArea.goToReviewItem(id)`. The previous and next buttons
  (`reviewPrevious`, `reviewNext`) step through the anchored items in list
  order, wrapping around, and send `navigateRequested` for each.
- **EquationEditor.** Bind `equation` to `equationSnapshot(id)`, re-read on
  `revision`, and bind `symbols` to `equationSymbols`. For live validation,
  set `errorText` from `validateEquation(sourceDraft)`. Route
  `sourceChangeRequested` to `setEquationSource` and `symbolInsertRequested`
  to `equationSourceWithSymbol`. Palette entries go to the view that last
  had focus (`activeView`): the visual view inserts them itself, and the
  source view sends `symbolInsertRequested`.

## Scope

- **Notes** are plain text. Rich formatting inside a note, and note
  separators or continuation notices, are not modelled.
- **Tracked structure.** The structure edits listed under
  [tracked changes](#tracked-changes) are refused while tracking rather
  than recorded.
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
selection). `tst_rich_text_review_tracking` covers tracked paragraph breaks
(accepting a cross-paragraph deletion equals the untracked delete),
formatting changes, table row deletion and the typed refusals, equation and
reference edits, tracked moves that keep field and comment identity, and
navigation from the document and the editing surface.
`tst_rich_text_notes` covers note numbering, undo, typed refusals,
persistence, footnotes at the foot of their page, endnotes and continuous
layouts. `tst_rich_text_equation_editor` covers typing, caret movement
through slots, templates, deletion, the visual and source round trip, hit
testing and the canvas's keys. `tst_review_example.qml` drives the example,
including navigation, a footnote from the manager and a visual equation
edit, and `tst_document_review.qml` covers the panels on their own.
