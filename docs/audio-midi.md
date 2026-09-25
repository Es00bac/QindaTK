<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# MIDI, patterns and clip launching

WP-064: the MIDI data of an `AudioSession`, its scheduling in the real
engine, the clip launcher and MIDI file interchange. `PianoRoll`,
`MidiControllerLane`, `StepSequencer` and `ClipLauncher` stay request-based
views; their requests map one-to-one onto the models below. Builds on the
engine ([audio-engine.md](audio-engine.md)), mixer
([audio-mixer.md](audio-mixer.md)) and the tempo map
([tempo-automation.md](tempo-automation.md)). Decisions D-150 to D-153 in
[decisions.md](decisions.md).

```
 session.midi (MidiSequenceModel) ── patterns · scenes · slots · history · "midi" part
   │ compile (GUI)                         session.launcher (MidiLauncherModel)
   ▼                                          │ launch/stop ─▶ AudioEngine ─ MIDI ring ─▶ device thread
 AudioRenderGraph: MidiClock, programs,       ◀── queued/started/stopped/canceled ── MIDI event ring
   placements (MIDI clips), slot programs     render(…, MidiLaunchState) ─▶ test instrument ─▶ track channel ─▶ mixer
```

## Data (D-150)

| Thing | ID / unit | Notes |
| --- | --- | --- |
| Pattern | UUID; clip resource `pattern:<uuid>` | `length` ticks (notes and events loop on it), `seed`, name |
| Note | UUID | `tick` (0 ≤ tick < length), `duration` > 0, pitch 0–127, channel 0–15, velocity 1–65535 (16-bit) |
| Controller event | UUID | `cc:<0-127>` (7-bit), `pitchBend` (14-bit), `channelPressure` (7-bit), `noteBend` / `notePressure` (32-bit, per note); value stored 32-bit |
| Step row / step | UUID | pitch, channel, `stepTicks`, 1–64 steps; step `enabled`, velocity, `gate` (0, 1], `probability` [0, 1] |
| Scene / slot | UUID | one slot per MIDI track and scene, naming a pattern |

Ticks are musical ticks at the session PPQ (TimeMap `ticksPerQuarter`,
960 by default). A controller lane edits in normalized 0–1 at the
controller's resolution (`controllerDescriptor(id).resolutionBits`); a
32-bit per-note value survives that path bit-exactly. MIDI 1.0 values scale
up with MIDI 2.0 min-centre-max scaling (`Midi::scaleUp`), so 7- and 14-bit
data round-trips exactly. A per-note event lies inside its note
(`[tick, end)`), on its channel; a move shifts it, a resize clamps it and a
delete removes it with the note.

## Operations and history

All return `{ok, error, …}`; a refused operation changes nothing. Each
accepted one is one step of the model's own undo stack (`undo`, `redo`,
`canUndo`, `undoText`), which restores the complete previous content.

| Request (control) | Model call |
| --- | --- |
| `PianoRoll.noteInsertRequested` | `insertNote(pattern, {startTicks, endTicks, pitch, velocity})` |
| `notePreviewRequested` / `noteEditFinished` / `noteEditCanceled` | `previewNoteEdit(requestId, pattern, note, "move"｜"resizeEnd"｜"resizeStart", ticks, pitch)` / `finishEdit(requestId)` / `cancelEdit(requestId)` |
| `noteDeleteRequested` | `deleteNotes(pattern, [note])` |
| `MidiControllerLane.valuePreviewRequested` / `editFinished` | `previewEventEdit(requestId, pattern, controller, event, ticks, value)` / `finishEdit` (`"velocity"` edits notes) |
| `eventValueRequested` / `eventDeleteRequested` | `setEvent(pattern, {id, controller, tick, value｜raw, noteId})` / `deleteEvents` |
| `StepSequencer.stepEditRequested` | `setStep(pattern, row, step, field, value)` |
| (host) | `moveNotes`, `setNote`, `quantizeNotes(pattern, ids, grid, strength %, ends)`, `addRow`, `setRow` (step count: polymetric length), `removeRow`, `setPattern({name, length, seed})`, `addScene`, `setSlot`, `clearSlot`, `quantization` |

A drag previews: `noteRows`, `controllerEvents` and `stepRows` show the
preview (`previewChanged`) while the engine keeps the committed content;
every update plans from the committed pattern; finish records one step.
Quantize is exact integer arithmetic: a position moves by
`round((line − position) · strength / 100)` toward the nearest grid line.
Snapshots: `noteRows(pattern, start, end)` (PianoRoll `notes`),
`tickBoundaries(start, end, grid)`, `controllerEvents(pattern, controller,
start, end)`, `stepRows(pattern)`; `patternData(pattern)` is the committed pattern.

The session saves the content as its `midi` part
(`schema: qindatk.midi/1`, ticks as decimal strings, a different PPQ is
refused); a session without it opens empty.

## Scheduling (D-152)

`session.addMidiTrack(name)` adds a `midi` track: a mixer channel like an
audio track (fader, pan, sends, mute, solo). `session.insertPattern(track,
pattern, startTicks, lengthTicks)` adds one undoable beat-locked clip; the
pattern plays from the clip's beat (from `start − sourceOffset`), looping
on its length, heard for notes that start inside the clip and cut at its
end. Step rows loop on their own length.

Graph compilation builds a `MidiClock` from the committed tempo map:
`sampleAt(tick)` is TimeMap's exact arithmetic, so a note lands on the same
sample as `EngineScheduleSnapshot::sampleAtMusicalTick`, through every
tempo and meter change. `render()` evaluates the notes overlapping each
chunk and the **built-in test instrument** (a cosine at the pitch, level
0.25 × velocity, 1 ms attack, 5 ms release) computes every sample from the
note alone. It is stateless: any block size, a seek, a loop or a recompile
gives identical samples, the device equals the offline render, and a note
is silent from `end + release − 1`. Overlapping notes (also on one key)
sum. `graph.midiEvents(channel, start, frames, …)` returns the same
schedule as events (note-offs, controllers, note-ons, then per-note
controllers at one sample) — the stream WP-066 instruments receive; the
test instrument uses notes only.

**Probability (D-151).** Step *s* of a row plays on pass *n* (counted from
the placement origin) when SplitMix64(seed, row, s, n) < probability. The
same placement plays the same steps everywhere; `setPattern({seed})`
re-rolls as one undo step.

## Clip launching (D-153)

`session.launcher` lists `slotList` (ClipLauncher `slots`: `state` ready |
queued | playing, `available`, `reason`, `startSample`) and takes
`launch(requestId, slot, quantization)`, `stop(requestId, slot, …)`,
`stopAll`; `quantization` none | beat | bar (default: `midi.quantization`).
The device thread computes the boundary from its own position when the
command arrives and answers once; `launchAcknowledged(requestId, accepted |
rejected | canceled, message)` feeds `ClipLauncher.acknowledgeLaunch`.

| Engine event | Launcher |
| --- | --- |
| Queued (boundary sample) | slot `queued`; request `accepted` ("Starts at sample …") |
| Started (exact sample; the period is split there) | slot `playing`, the track's other slot `ready` |
| StopQueued / Stopped | `stopping`, then `ready`; the replaced clip keeps only its release |
| Canceled | a waiting request replaced by a later one on the track, or ended by a transport stop |
| Rejected | no graph yet, slot not on a MIDI track of the graph, or 32 launching tracks |

Launching while the transport is stopped queues the clip; it starts when
playback reaches its boundary. Launched clips and the arrangement sum on a
track; a file render is the arrangement. At most 32 launcher requests wait
for their answer.

## Audition and all-notes-off

`engine.auditionNoteOn(pitch, velocity)`, `auditionNoteOff(pitch)`,
`allNotesOff()`: up to 16 test-instrument voices on the first two outputs
(a cue path, not the mixer), with the transport running or stopped;
`auditionVoices` and `launchedClips` report what sounds. A transport stop,
a device stop or loss (`midiReset`), a reconfiguration and detaching the
session from the engine release every audition note and launched clip;
nothing resumes on recovery. `PianoRoll` pairs every `auditionRequested(pitch,
velocity)` with `auditionReleased(pitch)` — on finish, cancel, a pitch
change, disabling and destruction.

## Interchange

`session.exportMidi(pattern, file)` writes a Standard MIDI File (format 1,
the session PPQ): a conductor track with the tempo map and meters, and the
pattern's notes, CC, pitch bend, channel pressure and per-note pressure (as
polyphonic key pressure). MIDI 1.0 limits are reported, never silent:
`reducedVelocities`, `reducedValues`, `droppedNoteBends` (no MIDI 1.0
message), `droppedSteps` (step rows are not exported), `ambiguousOverlaps`
(nested notes of one key, which FIFO pairing re-pairs on import).
`session.importMidi(file)` reads formats 0 and 1 (running status, note-on
velocity 0, FIFO note pairing, key pressure attached to the sounding note)
into one pattern per track (one undo step), rescales to the session PPQ
exactly when the division allows (else nearest, `roundedPositions`) and
reports tempo and meter events (`tempo`) without changing the tempo map.
`midi.capabilities` lists the declared support.

## Verification (run by the round build)

| Test | Checks |
| --- | --- |
| `tst_midi_model` | scaling and 32-bit lane precision; insert, move, resize, delete with attached expression; refusals; undo to the exact content; quantize strengths; drag preview and one-step commit; polymetric rows and undo; JSON roundtrip and PPQ refusal; SMF roundtrip with reports; running status and division rescale |
| `tst_midi_schedule` | event samples at 120 BPM and after a tempo change (matching the schedule snapshot) and after undo; first sample exactly at the note start, block-size and seek independence, overlapping notes, silence after the release; 3-against-4 rows; seeded probability, re-roll and undo; launched slot from its boundary and its cut; session save/reopen |
| `tst_midi_engine` | three launches from a stalled GUI all quantized to the beat, each started at its queued sample, the last winning, queued reported before playing; a quantized stop on the grid; audition, note-off, all-notes-off and transport stop; device loss and recovery leave nothing sounding; detaching the session stops its clips; zero device-thread allocations |
| `tst_midi_session.qml` | PianoRoll insert, previewed drag and undo through `session.midi`; paired audition signals including destruction mid-gesture; StepSequencer toggle and undo; ClipLauncher without a device refuses with an explanation |

## Not covered here

Plugin instruments and their event input are WP-066 (they consume
`midiEvents`). MIDI device input/output and MIDI recording are not part of
the reference engine (`capabilities.midiDevices`). MIDI clip lengths are
absolute timeline ticks (a clip does not stretch with tempo; the notes in
it follow the beat). Performance measurement, other platforms, screen-reader
runs and output-fidelity comparison are WP-088…WP-094.
