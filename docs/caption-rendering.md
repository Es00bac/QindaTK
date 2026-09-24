<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Caption frame rendering

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
hosts provide decoded frames to `renderFrame`; subtitle sidecar export remains
separate from burned pixels. Accessible cue text stays available in the
CaptionEditor independently of the rendered image.
