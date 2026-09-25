<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Notation: score model, layout, output, interchange and playback

WP-067 (AUD-27, ENG-11). `Tk.ScoreModel` is a persistent score of written
music; `Tk.ScoreView` draws its page layout and sends edit requests;
`Tk.ScorePlayback` plays it through an `AudioSession`'s MIDI model and
engine ([audio-midi.md](audio-midi.md)) under the session tempo map
([tempo-automation.md](tempo-automation.md)). Decisions D-178 to D-181 in
[decisions-domains.md](decisions-domains.md).

```
ScoreView ──requests──▶ ScoreModel (score_types / rhythm / codec / model_*)
    ▲                       │ layoutScore (D-179) ──▶ pageSnapshot ──▶ ScoreView
    └── layoutSnapshot(page)│                     └─▶ paintPage ──▶ PDF, images
                            │ MusicXML write/read (D-181), qindatk.score/1 JSON
                            ▼
                       Score::toMidi (D-180) ──▶ ScorePlayback ──▶ session.midi pattern + clip ──▶ engine
```

## Score model (D-178)

| Object | ID | Holds |
| --- | --- | --- |
| Score | UUID | title, composer, measures, parts |
| Measure | UUID, shared by every staff | time signature (n/1…32), key (−7…7 fifths), break after (none, system, page) |
| Part | UUID | name, abbreviation, MIDI channel, 1–4 staves |
| Staff | UUID | clef (treble, bass, alto), voices 1–4 (voice 1 always exists) |
| Voice | UUID | events sorted by tick, never overlapping, never crossing a bar line |
| Event | UUID | tick, note value, rest or chord of 1–16 notes, articulations, dynamic |
| Note | UUID | spelled pitch (step, alteration −2…2, octave), tie to the next event |

Ticks are exact integers at 960 per quarter (`Score::kPpq`), absolute from
the start of the score. A note value is a base (whole … 64th), 0–3 dots and a
tuplet ratio `actual:normal` of a unit; its ticks must be a whole number
(`tuplet-7:4-16th` is refused). Value IDs: `quarter`, `dotted-half`,
`double-dotted-eighth`, `triplet-eighth`, `tuplet-5:4-16th`,
`tuplet-3:2:eighth-quarter` (a triplet quarter counted in eighths).
Articulations are `staccato`, `staccatissimo`, `tenuto`, `accent`,
`marcato`, `fermata` (only `fermata` on a rest); dynamics `ppp` … `fff`.

Invariants checked on every change (`Score::validate`): unique IDs, voice 1
complete in every measure, no overlap, tuplet groups complete and inside one
measure, ties ending on the same sounding pitch in the next event, the last
measure without a break. A refused change changes nothing.

## Rhythm

Entry overwrites: a new event at `[tick, tick + value)` removes every event
it overlaps (a whole tuplet group when it cuts into one) and the uncovered
remainder becomes rests. Rests take the largest value aligned to its
offset in the measure (`Score::restFill`), so a quarter at beat 1 of an empty
4/4 bar leaves a quarter and a half rest. A tuplet event claims its group
span (`normal × unit`, aligned in the measure): the rest of the group fills
with tuplet rests, and entering another value of the same ratio into that
group replaces only what it covers. `rhythmReport()` lists every measure,
staff and voice as `complete`, `partial` (voices 2–4 may be partial) or
`empty`.

## Editing

All results are `{ok, error, …}`; each accepted call is one undo step
(`undo`, `redo`, `undoText`). ScoreView requests map one-to-one:

| ScoreView request | ScoreModel call |
| --- | --- |
| `noteEntryRequested(…, partId, staffId, voiceId, measureId, ticks, pitch, durationId)` | `insertNote(partId, staffId, voiceId, measureId, ticks, pitch, durationId)` |
| `restEntryRequested(…)` | `insertRest(…)` |
| `notationEditRequested(requestId, scoreId, eventId, edit, value)` | `editEvent(eventId, edit, value)` |
| `eventDeleteRequested(requestId, scoreId, eventId)` | `deleteEvents([eventId])` |
| `navigateRequested(scoreId, eventId, direction)` | `adjacentEvent(eventId, direction)` (previous, next, voice-up, voice-down) |
| `playbackLocateRequested(measureId, ticks)` | `ScorePlayback.timelineTicksFor(ticks)` |

`editEvent` edits: `duration` (value ID), `dot` (toggle), `tie` (all notes or
one note ID), `articulation` (toggle), `dynamic`, `pitch` (name, or
`{noteId, pitch}`), `step` (±n diatonic, spelled in the key), `transpose`
(semitones, spelled in the key), `respell` (next enharmonic), `addPitch`,
`removePitch`, `rest`. Deleting a note leaves a rest of the same value and
ID; deleting a rest removes it only in voices 2–4. Overwriting or deleting a
tied-to note drops the tie. Structure: `newScore`, `addPart`, `removePart`,
`setPart`, `setClef`, `addVoice`, `appendMeasures`, `insertMeasures`,
`removeMeasures`, `setTimeSignature` and `setKeySignature` (the measure and
the following ones that had the same value; a meter changes only on
measures of rests), `setBreak`.

ScoreView keys: arrows navigate, Shift+Up/Down move a step, `.` dot, `T` tie,
`J` respell, `1`…`7` set the value (64th … whole) of the selection and of
entry, Delete/Backspace delete, Enter enters at the host cursor. The
selected event's `label` is the view's accessible description.

## Layout (D-179)

`Score::layoutScore` places everything in points on the page (A4 default,
`pageWidth`, `pageHeight`, `staffSpace` 7 pt, `margin` 42 pt):

- Every event start in any staff of a measure is a column shared by all
  staves and voices; a column's width grows with the log of its duration,
  plus fixed room for accidentals and dots.
- Systems break greedily at the page width and at every forced system or
  page break; every system but the last (unless it is at least 3/4 full)
  is justified to the full width. Pages break when the next system does not
  fit or after a page break.
- Clef and key begin every system; a time signature shows where it changes;
  a key change to C shows naturals.
- Accidentals follow the key, earlier alterations of the same line and
  octave in the measure (every voice of the staff) and ties (a tied-to note
  shows none). Stems: voice 1 up and 2 down when two voices sound in a
  measure, else by the notes' distance from the middle line. Eighths and
  shorter beam by beat (dotted quarter in compound eighth meters), with
  secondary beams and stubs; unbeamed notes get flags. Seconds in a chord
  move to the other side of the stem. Ties that cross a system break draw
  as two halves; tuplet brackets go on the stem side.
- Options `{parts: [id]}` lay out a part on its own (part extraction).

`layoutSnapshot(page, options)` is ScoreView's snapshot: page, pageCount,
title, systems → staves (`top`, `lineGap`, `x0`, `x1`, `ties`, `beams`,
`tuplets`) → measures (`number` at a system start, `clefX`, `keyGlyphs`,
`time`, exact `entryPositions` at every column and beat) → voices → events
(`x`, `staffStep`, `heads`, `stemDirection`, `stemX`, `stemEndStep`,
`flags`, `dotCount`, `articulations`, `dynamic`, `label`). Coordinates are
normalized to the page; step 0 is the bottom line. `locate(tick)` gives the
page and x of a score tick (the playback cursor). `readingOrder()` lists
every event (measure, part, staff, voice, time) with its spoken label.

## Output

`exportPdf(file, options)` writes one PDF page per layout page and
`renderPageImage(page, zoom)` (C++) an image, both through `Score::paintPage`
from the same layout the view draws.

## Native file

`save(file)` / `open(file)` / `toJson()` / `loadJson()`: `qindatk.score/1`,
960 PPQ (another resolution is refused), ticks as decimal strings, value
IDs and pitch names as above. Opening replaces the content and clears the
history.

## Interchange (D-181)

`exportMusicXml(file, options)` writes MusicXML 4.0 partwise (divisions 960)
and `importMusicXml(file)` reads it as one undo step. Declared feature set:
parts with MIDI channel, 1–4 staves and clefs G2/F4/C3, voices (`staff × 4 +
voice`), notes, chords, rests, spelled pitches, types with dots and
time-modification, ties, articulations (strong-accent is marcato),
fermatas, dynamics, keys, meters, system and page breaks, title and
composer. Native IDs travel as `id` attributes (`N-`, `M-`, `P-` prefixes)
and `qindatk-score`, `qindatk-staff`, `qindatk-voice`, `qindatk-event`
processing instructions, so a QindaTK file returns with every ID. Import
reports `unsupported` element names (grace notes, slurs, lyrics, repeats,
clef changes, words, …) instead of dropping them silently,
`durationMismatches` (duration against the written value),
`roundedPositions` and `filledRests` (voice-1 gaps filled).

## Playback (D-180)

`Score::toMidi(content, ppq)` is the one conversion: a tie chain becomes one
MIDI note from its first start to its last end; staccato sounds half and
staccatissimo a quarter of the last written value; the dynamic in force in
the voice (default mf) sets the velocity, accent ×1.15 and marcato ×1.3; ticks
rescale exactly to the session PPQ or the conversion fails. MIDI note IDs
are UUIDv5 of the chain's first score note, so re-conversions keep them.
Spelling, voices, tuplet ratios, articulation symbols and fermatas stay in
the score (`retained`). `midiConversion(ppq)` shows the result.

`ScorePlayback {score; session}`: `sync()` writes the conversion into one
pattern of `session.midi` (one MIDI undo step), adds a MIDI track named
after the score and one beat-locked clip at `startTicks`, replacing the
clip when the score's length changes; `autoSync` re-syncs on every score
change. The engine then schedules the notes like any pattern, through every
tempo and meter change. `scoreTickAt(timeline)`, `timelineTicksFor(tick)`,
`currentScoreTick()` and `cursor()` map the transport to the score and to
`locate`. `audition(eventId)` sounds the event's notes on the engine's cue
voices until `releaseAudition()`.

## Verification (run by the round build)

| Test | Checks |
| --- | --- |
| `tst_score_model` | value ticks and IDs, pitch spelling and enharmonics; overwrite entry and rest filling with exact results; undo to the previous content; refusals; tuplet groups and replacement; key and measure accidental context; ties required to match, dropped on overwrite; respell, step, transpose; chords, articulations, dynamics, voices, deletion; measures, meters, keys, breaks; native JSON and file round trip, PPQ refusal |
| `tst_score_layout` | shared columns in a dense two-voice measure, stems by voice, beams by beat, rest position; tuplet bracket and a tie across a system break; 240 measures over pages with forced system and page breaks and justified systems; snapshot entry positions, labels, reading order and locate; part extraction to layout and MusicXML; PDF and image pixels from the layout |
| `tst_score_interchange` | MusicXML round trip of the declared set with every ID; a foreign file's grace, slur and lyric reported; MIDI conversion (tie merge, staccato, triplets, 480 PPQ, inexact PPQ refused, stable IDs); session playback at exact samples, through a tempo change, cursor mapping, auto re-sync; chord audition on a running engine |
| `tst_score_edit.qml` | ScoreView keyboard entry, dot, step, value keys, delete and undo through ScoreModel; rest entry and refusal; a click entering at an exact position |

## Not covered here

Grace notes, slurs, lyrics, chord symbols, repeats and endings, cross-staff
notes, clef changes inside a score, transposing instruments and multi-measure
rests are declared unsupported (`capabilities.unsupported`) and reported on
import. Engraving uses plain glyphs (letters and Unicode accidentals), not a
SMuFL music font. Performance measurement, other platforms, screen-reader
runs, translation and output-fidelity comparison with other notation
programs are WP-088 to WP-094.
