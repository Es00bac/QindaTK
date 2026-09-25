<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Slides and presentations

The slide stack is one persistent model, one evaluator, one painter and one
playback controller. The five slide controls (`SlideSorter`, `SlideCanvas`,
`SlideMasterEditor`, `AnimationPane`, `PresenterView`) stay request-based;
`SlideDeck` supplies their snapshots and applies their requests. See
[controls.md](controls.md#slide-authoring-and-presentation) for the control
contracts and [examples/slide-show](../examples/slide-show/Main.qml) for the
wiring.

| Type | Files | Role |
| --- | --- | --- |
| `SlideDeck` | `slide_deck*.cpp`, `slide_deck_types.*` | Persistent deck, `DocumentModel` of schema `qindatk.slides.v1` |
| `Slides::evaluate` | `slide_timeline.*` | Exact-tick build schedule and frame evaluation |
| `Slides::paintFrame` | `slide_render.*` | The only slide painter: views, images, previews, PDF |
| `SlideShow` | `slide_show.*` | Deterministic presentation position, navigation and clock |
| `SlideFrameView` | `slide_frame_view.*` | Paints a show frame or a static slide |
| `PresentationWindows` | `presentation_windows.*` | Native audience and presenter windows through `WindowHost` |
| `PresenterConsole` | `src/qml/PresenterConsole.qml` | Presenter window content bound to a show |

## Model

A deck has a slide size (default 960 × 540), masters and slides.

- A **master** has an ID, title, theme ID, background and foreground colour,
  **placeholders** (role, label, kind `text`/`shape`, bounds, fill, font
  size, default text) and **layouts**. A layout lists the roles it shows and
  where they sit (role + bounds).
- A **slide** names its master and layout and holds objects, effects,
  speaker notes, a transition, a section, an optional stored title and a
  hidden flag.
- An **object** is a text box or a shape with bounds, fill, font size, rich
  text, lock and visibility. A non-empty `role` binds it to a placeholder.

### Inheritance (D-158)

A placeholder-bound object takes its bounds from the slide's layout (else
the master placeholder), and its text, fill, kind and font size from the
master placeholder. Each of bounds, text and fill has an own flag: moving
an object sets `ownBounds`, editing its text sets `ownText`, setting a fill
sets `ownFill`. An own value always wins, so editing a master or layout
updates every slide except its deliberate overrides.
`resetOverride(slideId, role)` clears the three flags. `applyMaster` keeps
overrides, keeps a layout ID the new master also has (else the layout with
the same title, else its first) and adds objects for roles the new layout
shows that the slide lacks. `objects(slideId)` returns resolved objects with
`inheritedBounds/Text/Fill`; `masters` reports each placeholder's
`overrideCount` and `placeholderOverrides(masterId)` lists text overrides.

### Text boxes and notes

Text is stored as the rich-text stack's canonical fragment JSON
(`qindatk.richtext.fragment`, [rich-text.md](rich-text.md#persistence)) with a
derived plain-text copy for snapshots. `setObjectText` and `setNotes` accept
plain text. `beginTextEdit(objectId)` returns a deck-owned
`RichTextDocument` with the box's resolved content; bind it to
`Tk.RichTextEditor`, then `commitTextEdit()` stores it as one deck step or
`cancelTextEdit()` drops it. While a box is being edited, deck edits, undo
and redo are refused.

### Edits and undo

Every edit builds a candidate state, validates it (unique IDs, references,
bounds, colours, enums and time ranges) and commits it as **one**
`DocumentCommand` through `UndoService`
([document-models.md](document-models.md#commands)). A rejected edit changes
nothing and sets `lastError`. Object and effect IDs are unique in the deck,
so `moveObject`, `setObjectText`, `editEffect` and the like take no slide
ID. A standalone deck has a private 128-step history; a `DocumentSession`
adopts it. `duplicateSlide` copies objects and effects with new IDs and
remaps effect targets; deleting an object deletes the effects that target it.

## Timing (D-159)

Times are signed 64-bit ticks at 705,600,000 per second (the timeline
timebase, [time-map.md](time-map.md)). Seconds from QML are rounded to the
millisecond (705,600 ticks); `delayTicks`/`durationTicks` decimal strings set
exact values. A slide's builds:

- **Step 0** plays when the slide appears: the transition first, then the
  effects before the first `click` effect.
- Each `click` effect starts a new step at time 0 of that step.
- `with` is triggered at the previous effect's trigger, `after` at its end.
  An effect starts `delay` after its trigger and lasts `duration`.

`evaluate(deck, slideId, step, ticks, fromSlideId)` is pure: steps before
`step` are complete, later steps have not started, and the current step is
sampled at `ticks`. An object whose first effect is an entry effect
(`appear`, `fade-in`, `fly-in`, `zoom-in`) is hidden until it starts;
`fade-out` hides it at its end. Progress is `(ticks − start) / duration` as a
double; `linear` uses it directly and the other easings (`in-cubic`,
`out-cubic`, `in-out-cubic`) evaluate a cubic Bézier through
`CurveEvaluator` ([curve.md](curve.md)). `schedule(slideId)` reports the step
count, step durations and every effect's start and end in ticks.

## Output (D-160)

`Slides::paintFrame` paints a frame into a rectangle, letterboxed in black at
the slide's aspect ratio: master background, then each visible object with
its frame opacity, offset and scale; text through `QTextDocument` from the
fragment's HTML. A `fade` transition draws the outgoing slide complete and
the incoming one over it; `push` slides both. `SlideFrameView`,
`SlideDeck.renderFrame`, `previewDataUrl`, the sorter thumbnails, the editing page of a `SlideCanvas` given a `deck`, and `exportPdf` all use it, so a
sampled frame, the audience window and the export agree pixel for pixel.
`exportPdf(path, includeNotes)` writes one page per shown slide with every
build complete (0.75 pt per slide pixel); with notes, a notes page (slide
thumbnail and notes) follows each slide.

Each `slides` summary carries `previewUrl`: a 160 px wide PNG data URL of
the slide with every build complete, for `SlideSorter`. The deck caches one
per slide and reuses it while the slide, its master and the slide size are
unchanged; entries of deleted slides are dropped, so the cache never holds
more than the deck's slides.

## Playback

`SlideShow` holds a position (slide, step, ticks into the step) over a deck.
`start`, `next` (completes a running build, then the next build, then the
next shown slide with its transition), `previous` (previous build or the
previous slide, complete), `goToSlide`, `goToIndex`, `stop`. Hidden slides
are skipped by `next`/`previous`; `goToSlide` can show one. `frame` is
`evaluate` at the position (plus `blackedOut`, also set after the last
slide). Time moves only through `advance(ticks)` / `advanceMilliseconds`;
with `liveClock` (default) a 16 ms timer converts monotonic nanoseconds to
ticks in one rounding per run, so frames never drift. The presenter clock
(`elapsedText`, `timerState`, `timerAction("start"|"pause"|"resume"|"reset")`)
advances with the same calls. Editing the deck during a show keeps the
position valid (a deleted slide continues at the first shown slide).

## Windows (D-161)

`PresentationWindows.open()` creates two native windows through `WindowHost`
(roles `slide-audience`, `slide-presenter`) and starts the show. The audience
window holds only a `SlideFrameView` (objectName `slideAudienceOutput`); the
presenter window holds `presenterDelegate` (normally `Tk.PresenterConsole`,
created with the initial properties `show` and `windows`). Speaker notes are
bound only in the presenter window.

Displays are identified by `QScreen::name()`. `audienceScreenId` chooses the
audience display; empty means any display other than the presenter's.
`audienceStatus`: `connected` (full screen on its display), `shared` (one
display only: a normal window on the presenter's display), `waiting` (the
chosen display is missing: the window stays open on the presenter's display
and returns automatically when a display of that name is added) and `closed`
(no audience window; `reconnect()` reopens it). `audienceDisconnected` and
`audienceReconnected` report the transitions; `displays` lists connected
screens plus a missing chosen one with `connected: false`. Closing the
presenter window ends the presentation. The headless tests simulate display
removal and return with QPA test screens; real monitors, window managers and
mixed DPI are qualification work (WP-088 to WP-094).

`PresenterConsole` keys: Right, Down, Space, Page Down (next); Left, Up,
Page Up, Backspace (previous); B or period (blank); Home/End; Escape (end).

## Persistence

`qindatk.slides.v1` JSON (`.qtkslides`, `SlideDeck::encodeState` /
`decodeState`): `format`, `version` 1, `documentId`, `revision`,
`resetGeneration`, `timebase`, `extensions`, `size`, `masters`, `slides`.
Bounds are `[x, y, width, height]`, times decimal tick strings, text the
fragment JSON, overrides an `own` list. Encoding is canonical (sorted keys),
so reopen then save is byte-identical. Decoding validates everything before
use (64 MiB, 2,000 slides, 500 objects and 500 effects per slide, 64 masters
and layouts). The codec is registered in the built-in registry, so
`DocumentManager`, save, autosave, recovery and reopen work unchanged.

## Not in this slice

Image and media objects, the ink layer behind `PresenterView`'s annotation
tool (its request is not handled by `PresenterConsole`), custom easing
curves per effect and motion paths.
