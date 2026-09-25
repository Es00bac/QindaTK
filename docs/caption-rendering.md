<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Captions: formats, edits and rendering

`CaptionTrackModel` owns a caption track (stable cue IDs, exact millisecond
`[in, out)` intervals, text, language, style, display lane);
`CaptionEditor` draws it and requests edits. This page covers the formats a
track reads and writes, its structural edits and warnings, and the three
output paths: sidecar files, an embedded subtitle stream and burned-in pixels.

## Formats

`CaptionTrackModel.formats` lists what each format carries:

| id | kind | carries | note |
|---|---|---|---|
| `json` | native | timing, text, language, style, lane, ID | `toJson`/`loadJson`, `saveToFile`/`loadFromFile` (atomic) |
| `webvtt` | sidecar | timing, text, language, ID | `Language:` header names the track; a cue in another language is one `<lang tag>` span |
| `srt` | sidecar | timing, text | no language field; the host names it on import |
| `matroska-subrip` | embedded | timing, text, language | a SubRip stream in an exported `.mkv`; stream language = the track's BCP 47 tag |
| `burned-in` | render | timing, text, style | pixels only; accessible text stays in the editor |

WebVTT import takes the track language from an explicit argument, else from
the `Language:` header, else `und`. Cue text is plain text: `&amp;`, `&lt;`,
`&gt;`, `&nbsp;`, `&lrm;` and `&rlm;` are decoded on import and `&`, `<`, `>`
escaped on export; other markup stays literal. So an export and re-import
keeps every cue's ID, timing, text and language (`tst_caption_track_model`
`webVttKeepsEveryLanguage`, with English, Japanese and Arabic cues).

## Structural edits

Each is one undo step (`undo()` restores the previous cue list); a refusal
changes nothing and sets `error` and a `rejected` acknowledgement. An import
(`importWebVtt`, `importSrt`, `importEmbedded`) is one undo step that also
restores the previous track language, so undone cues never export under the
imported language (`tst_caption_track_model`
`undoAfterImportRestoresTheLanguage`); `setLanguage` itself is not an undo
step.

- `splitCue(cueId, atMs, textPosition = -1)`: `atMs` strictly inside the cue;
  the right part gets a new ID. The text splits at `textPosition`, or at the
  whitespace nearest the time's proportion of the cue; text without
  whitespace stays whole on both parts.
- `mergeCues(first, second)`: same language only; the union interval under
  the first ID, texts joined by a line break in time order; refused if the
  union overlaps another cue in the lane.
- `shiftCues(ids, deltaMs)`: a signed decimal delta for the listed cues (all
  when empty); refused before time zero or into an overlap with an unmoved
  cue in the same lane.

## Warnings

`warnings` lists `{cueId, kind, message}`; `warningText` joins the selected
cue's messages (and the last error) for `CaptionEditor.warningText`.
`overlap`: shown together with another cue (a different lane).
`short`: under `kMinimumDurationMs` (833 ms). `reading-rate`: more than
`kMaximumCharactersPerSecond` (20) characters per second, line breaks not
counted. `line-length`: a line over `kMaximumLineLength` (42) characters.
`glyphs`: characters no installed font draws
(`CaptionBurnIn::missingCharacters`); burned in they would be empty boxes.
Glyph warnings need a `QGuiApplication` (fonts); a guiless host gets none.
Warnings inform; they never block an edit.

## Embedded captions

With `VideoFinishing.embedCaptions`, `MediaExport` writes the committed cues
as a Matroska SubRip (`S_TEXT/UTF8`) stream: one packet per cue, its UTF-8
text, pts and duration in milliseconds, timed from the export's first frame
(`MediaFinishing::subtitlesFrom`). A cue is muxed when the first frame at or
after its start is written, so the stream interleaves with the pictures;
cues starting after the last frame are dropped, ends are kept exactly.
Overlapping cues are separate packets. `MediaCaptions::readEmbedded(path)`
reads the first text subtitle stream (SubRip, plain text or WebVTT; bitmap and
ASS streams are refused as unsupported) with its language, and
`CaptionTrackModel.importEmbedded(path)` loads it, re-assigning lanes. An
export and re-import keeps timing, text and the BCP 47 language tag
(`tst_video_finishing` `burnedAndEmbeddedCaptionsReachTheExport`).

## Burn-in

`CaptionBurnIn::burn(image, cues, timeMs)` is the one caption painter;
`CaptionFrameRenderer`, `CaptionSequenceExporter` and program finishing
(`VideoFinishing.burnInCaptions`) all draw through it. On program frames the
time is the frame's start in whole program milliseconds (floor), which decides
`[in, out)` exactly: at 25 fps a cue `[120, 400)` is on frames 3–9. Burned-in
frames reach the monitor and the exported file through the same
`MediaProgram::render`.

Text is laid out once with `QTextLayout` (font fallback, shaping and bidi;
line breaks become U+2028; the paragraph direction is the text's first strong
character) and that layout is drawn for the outline and the fill.
`CaptionBurnIn::glyphRuns(text, pixelSize, emphasis, width)` returns the runs
of the same layout, so a test inspects what is painted: `tst_video_finishing`
checks that the Japanese, Arabic and Greek cues have no `.notdef` glyph, that
Arabic is one right-to-left run with the first letter drawn rightmost and in
its joined (initial) form, and that an uncovered character is reported.

## Frame renderer

`CaptionFrameRenderer` burns committed `CaptionTrackModel` cues into a
host-decoded `QImage` at an exact non-negative decimal millisecond string.
The cue interval uses `[inMs, outMs)`: a cue is visible at its start and
hidden at its end. The renderer keeps time as a string across QML and parses it
directly to a signed 64-bit integer before comparing with model cue values.

Overlapping cues occupy distinct model lanes. The renderer draws them in
stable lane, start-time and cue-ID order in normalized horizontal/vertical
caption bands. It respects the model's supported `default` and `emphasis`
style IDs, using white and warm-yellow outlined text respectively. The model
does not currently expose per-cue opacity, custom placement or outline width,
so the renderer does not infer those properties.

`renderFrame(frame, track, timeMilliseconds)` returns status, exact time,
active stable cue IDs and a PNG data URL for QML preview. `saveRenderedFrame`
atomically writes the most recent rendered frame as PNG and defaults to
`QStandardPaths::AppLocalDataLocation/caption-frame.png`. An invalid image,
track, time string, cue document or save path produces a visible error.

`renderFixture(track, timeMilliseconds)` supplies a synthetic scene solely for
the runnable `examples/caption-render/Main.qml` demonstration. Production
hosts provide decoded frames to `renderFrame`, or burn captions into a whole
program through `VideoFinishing`; sidecar and embedded export stay separate
from burned pixels. Accessible cue text stays available in the
CaptionEditor independently of the rendered image.

## PNG sequence handoff

`CaptionSequenceExporter.exportFrames(frames, track, times, targetDirectory)`
accepts an ordered list of host-decoded `QImage` values and matching exact
decimal millisecond strings. It validates each timestamp through the frame
renderer and writes `frame-000000.png` onward plus `timing.json`. Each manifest
row preserves the original time string, output filename and ordered active cue
IDs, so an encoder or mux host can map frames without timestamp rounding.
Frame count is capped at 10,000.

Output is staged in a sibling directory. Each PNG and the manifest is written
with `QSaveFile`; the completed directory is published with a directory rename
only after every frame succeeds. An existing target directory is rejected.
Failure removes staging output and reports an error. The synchronous exporter
does not encode video, mux audio, or provide mid-export cancellation; encoded
video with captions goes through program finishing and `MediaExport`. The
`exportFixtureSequence` helper and `examples/caption-sequence/Main.qml` use
synthetic imagery only; production hosts call `exportFrames` with their decoded
frames.
