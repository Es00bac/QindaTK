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

**Cross-model guards.** A pattern that a timeline clip places
(`pattern:<uuid>`) cannot leave the content: `removePattern`, and an `undo`
or `redo` that would drop it (undoing "Add pattern" after `insertPattern`),
are refused with the track named, and nothing changes — no clip is left
without its notes. A session PPQ change (`timeMap.ticksPerQuarter`) rescales
every stored tick — the content and every history step — so notes keep
their beat; when a position is not exact at the new PPQ the TimeMap refuses
the change (`rejected`) and nothing moves.

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
sum. A played note lasts at most its pattern's length (a longer one would
overlap its own repeat; the stored duration is kept), so a render looks
back at most one pass and the device thread's work per block does not grow
with playback time; step rows look back one gate.

**Controllers the test instrument plays.** Within a placement, from the
pattern origin on (a trimmed clip chases earlier values): `cc:7` volume ×
`cc:11` expression scale the level (value 0–1; 1 without an event), channel
`pitchBend` bends ±2 semitones and a note's `noteBend` ±48 (MIDI 2.0
default), with continuous phase: a bent note integrates its phase from its
own start over the same change ticks in every render, so it stays
stateless. Other controllers (`cc:*`, `channelPressure`, `notePressure`)
are scheduled but have no parameter in the test instrument.
`graph.midiEvents(channel, start, frames, …)` returns the full schedule as
events (note-offs, controllers, note-ons, then per-note controllers at one
sample) for any other instrument.

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

**Transport jumps.** Launcher state lives in device samples, so a loop wrap
or a seek re-bases it on the device thread: a playing clip keeps its
musical progress and is heard from the new position (notes sounding across
the jump are cut); a waiting launch or stop is re-quantized from the new
position and reported again (Queued / StopQueued, same request). A boundary
at or past the loop end therefore fires right after the wrap.

Launching while the transport is stopped queues the clip; it starts when
playback reaches its boundary. Launched clips and the arrangement sum on a
track; a file render is the arrangement. At most 32 launcher requests wait
for their answer.

## Audition and all-notes-off

`engine.auditionNoteOn(pitch, velocity)`, `auditionNoteOff(pitch)`,
`allNotesOff()`: up to 16 test-instrument voices on the first two outputs
(a cue path, not the mixer), with the transport running or stopped;
`auditionVoices` and `launchedClips` report what sounds. A transport stop,
a device stop or loss (`midiReset`), a reconfiguration, detaching the
session from the engine and destroying the session (`releaseMidi`, one
reset command that is not a counted request) release every audition note,
launched clip and MIDI take; nothing resumes on recovery. `PianoRoll` pairs
every `auditionRequested(pitch, velocity)` with `auditionReleased(pitch)` —
on finish, cancel, a pitch change, disabling and destruction.

## MIDI input and recording

`session.midiInput` (MidiInputPort) is one ALSA sequencer input port
(`openPort(name)`, `connectPort("client:port")`, `closePort`; `available`
is false in a build without ALSA, where a host calls `receiveNote(pitch,
velocity7)` itself). Note-on/off become audition notes on the session's
engine (16-bit velocity by MIDI 2.0 scaling); closing the port or losing the
engine releases what it holds (`heldNotes`). Device *configuration*
(DeviceSettingsPanel, PLT-19) is WP-060's; MIDI output is not declared.

`session.launcher.record(requestId, slot, quantization)` arms one take: the
device thread computes its start boundary (answer: `Records from sample …`)
and, while the transport plays past it, stamps every audition note it
applies with a take-relative tick (the period's position, so accuracy is one
device period; a loop wrap keeps the take counting forward).
`stopRecording(requestId, quantization)` ends it and rounds its length up
to the grid. The notes (FIFO note-off pairing, held notes ending with the
take) become a new pattern that replaces the slot's pattern — one undo step,
"Record clip"; `recordingFinished(slot, pattern, notes)` reports it and the
slot's state is `recording` meanwhile (ClipLauncher `requestRecord` /
`recordRequested`). A transport stop, device loss or detach ends the take
and keeps what was played. `capabilities.midiDevices` declares `input`
(ALSA sequencer) and `output` (false).

## Interchange

`session.exportMidi(pattern, file)` writes a Standard MIDI File (format 1,
the session PPQ): a conductor track with the tempo map and meters, and the
pattern's notes, CC, pitch bend, channel pressure and per-note pressure (as
polyphonic key pressure). MIDI 1.0 limits are reported, never silent:
`reducedVelocities`, `reducedValues`, `droppedNoteBends` (no MIDI 1.0
message), `droppedSteps` (step rows are not exported), `ambiguousOverlaps`
(nested notes of one key, which FIFO pairing re-pairs on import; counted
in O(n log n) with a sparse table, `MidiFile::nestedNotes`).
`session.importMidi(file)` reads formats 0 and 1 (running status, note-on
velocity 0, FIFO note pairing, key pressure attached to the sounding note)
into one pattern per track (one undo step), rescales to the session PPQ
exactly when the division allows (else nearest, `roundedPositions`) and
reports tempo and meter events (`tempo`) without changing the tempo map.
`midi.capabilities` lists the declared support.

## Verification (run by the round build)

| Test | Checks |
| --- | --- |
| `tst_midi_model` | scaling and 32-bit lane precision; insert, move, resize, delete with attached expression; refusals; undo to the exact content; quantize strengths; drag preview and one-step commit; CC and per-note event drags (re-clamped inside the note), cancel and `deleteEvents`; polymetric rows and undo; JSON roundtrip and PPQ refusal; SMF roundtrip with reports; running status and division rescale; nested-note count equal to the pairwise definition and linear on 65,536 notes |
| `tst_midi_schedule` | event samples at 120 BPM and after a tempo change (matching the schedule snapshot) and after undo; first sample exactly at the note start, block-size and seek independence, overlapping notes, silence after the release; 3-against-4 rows; seeded probability, re-roll and undo; launched slot from its boundary and its cut; CC7 level, channel and per-note pitch bend (frequency, continuous phase, block and seek independence); bounded look-back for a 2^30-tick note; clips keep their pattern (remove and undo refused); PPQ rescale with history, inexact PPQ refused; session save/reopen |
| `tst_midi_launch` | device-thread processor block by block: a launch and a stop quantized to the loop end fire after the wrap; a playing clip follows a backward seek; a stop before the boundary cancels the launch ("nothing was playing"); take-relative note stamps, forward through a loop wrap, stop sized to the bar |
| `tst_midi_engine` | three launches from a stalled GUI all quantized to the beat, each started at its queued sample, the last winning, queued reported before playing; a quantized stop on the grid; explicit cancel acknowledged; audition, note-off, all-notes-off and transport stop; device loss and recovery leave nothing sounding; detaching or destroying the session stops its clips; a take from the MIDI input path becomes the slot's pattern (one undo step); an ALSA client plays and records through `session.midiInput`; zero device-thread allocations |
| `tst_midi_session.qml` | PianoRoll insert, previewed drag and undo through `session.midi`; paired audition signals including destruction mid-gesture; StepSequencer toggle and undo; MidiControllerLane drag, cancel, set and delete through `session.midi`; ClipLauncher launch and record requests without a device are refused with an explanation |

## Not covered here

No planned package hosts plugin *instruments*: WP-066 (items 1–5) covers
plugin ABI hosting, processing and state without a MIDI event input, so
`midiEvents` is offered as the stream such a host would take and the
built-in test instrument is the declared player. MIDI output ports are not
declared (`capabilities.midiDevices.output` is false). MIDI clip lengths are
absolute timeline ticks (a clip does not stretch with tempo; the notes in
it follow the beat). A timeline undo that restores a clip whose pattern was
removed afterwards leaves that clip silent (the MIDI history can restore the
pattern). Performance measurement, other platforms, screen-reader runs and
output-fidelity comparison are WP-088…WP-094.
