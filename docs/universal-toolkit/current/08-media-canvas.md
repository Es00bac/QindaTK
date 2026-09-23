<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Media, canvas navigation and pen input

Status: **CURRENT — source review dated 2026-09-22.** These entries describe the
working tree, not promised capabilities or a runtime certification. Use
`import QindaTK as Tk` for main-module types. `T` below means QtQuick.Templates.

Each entry describes purpose, consumer behavior and limits. **Declared members**
list locally declared properties, including computed implementation helpers;
inherited Qt properties/signals are not repeated exhaustively. A `ro` suffix
means read-only at the declaration. Method lists can include internal helpers;
the consumer contract is the prose, the existing reference and the linked source.
Object names list source assignments; expressions ending in an ID/index generate
per-item names. Test links show source coverage, not a test run in this audit.

[Book index](../README.md) · [Type index](00-type-index.md) ·
[Source findings](../reports/04-source-audit.md)

## Thumbnail

Image tile with stable geometry and explicit loading state.

**Source:** [src/qml/Thumbnail.qml](../../../src/qml/Thumbnail.qml) · **Base:** `Rectangle` · **Kind:** QML component.

source may use an async image provider; crop, selected, caption, placeholderIcon and tooltip style it. loadState is empty/loading/ready/failed. Compact natural size 72x40. Decode scheduling, frame extraction and cache policy belong to an external provider.

**Declared members:** `source: url`, `placeholderIcon: string`, `caption: string`, `crop: bool`, `selected: bool`, `tooltip: string`, `loadState: string` (ro).

**Object names:** `"thumbnail"`, `"thumbnailPlaceholder"`, `"thumbnailImage"`, `"thumbnailCaptionBand"`, `"thumbnailCaption"`.

**Coverage pointers:** [tst_media.qml](../../../tests/qml/tst_media.qml).

## Filmstrip

Compact ordered strip of supplied thumbnail records.

**Source:** [src/qml/Filmstrip.qml](../../../src/qml/Filmstrip.qml) · **Base:** `Item` · **Kind:** QML component.

frames contain source/caption; crop, placeholderIcon and minimumFrameWidth configure reduction. count/shown expose size; when two or more cells fit the last is the tail frame, while a single cell shows the head. It does not extract frames; it currently uses a Row positioner.

**Declared members:** `frames: var`, `crop: bool`, `placeholderIcon: string`, `minimumFrameWidth: real`, `count: int` (ro), `shown: int` (ro).

**Object names:** `"filmstrip"`, `"filmstripFrame" + cell.index`.

**Coverage pointers:** [tst_media.qml](../../../tests/qml/tst_media.qml).

## Waveform

Canvas-rendered symmetric audio envelope display.

**Source:** [src/qml/Waveform.qml](../../../src/qml/Waveform.qml) · **Base:** `Item` · **Kind:** QML component.

peaks are pre-reduced magnitudes on a fixed 0..1 scale; color/showCentreLine/tooltip configure it and empty reports no data. Natural height 28 px compact. Empty data draws only the centre line; one peak draws a full-width plateau; multiple peaks retain the per-column mirrored shape. It does not decode, perform peak-preserving reduction, select samples or edit audio.

**Declared members:** `peaks: var`, `color: color`, `showCentreLine: bool`, `tooltip: string`, `empty: bool` (ro).

**Object names:** `"waveform"`, `"waveformCentreLine"`, `"waveformShape"`.

**Coverage pointers:** [tst_media.qml](../../../tests/qml/tst_media.qml).

## TimeRuler

Adaptive unit ruler with a visible playhead and pointer scrubbing.

**Source:** [src/qml/TimeRuler.qml](../../../src/qml/TimeRuler.qml) · **Base:** `Item` · **Kind:** QML component.

pixelsPerUnit and originUnits are shared external view state; playheadUnits<0 hides the playhead; formatUnit formats labels. A 1/2/5/10 step ladder follows zoom; scrubbed emits units. Compact height 22 px. No rational frame clock, tempo map or drop-frame timecode model is included.

**Declared members:** `pixelsPerUnit: real`, `originUnits: real`, `playheadUnits: real`, `formatUnit: var`, `tooltip: string`, `step: real` (ro).

**Declared signals:** `scrubbed(real units)`.

**Object names:** `"timeRuler"`, `"timeRulerTicks"`, `"timeRulerPlayhead"`.

**Coverage pointers:** [tst_media.qml](../../../tests/qml/tst_media.qml).

## Ruler

Horizontal/vertical unit ruler for pages and canvases.

**Source:** [src/qml/Ruler.qml](../../../src/qml/Ruler.qml) · **Base:** `Item` · **Kind:** QML component.

origin is pixel position of unit zero; pixelsPerUnit, majorEvery/minorEvery, labels, cursorPosition and unit control ticks. band/activeBand shade page/column ranges; horizontal markers emit markerMoved, tabAdded and tabRemoved in pixels. Marker ownership is external. Changing pixelsPerUnit now repaints the canvas at fixed geometry; the runtime regression covers it. The default marker size and implicit ruler length follow `Theme.size.rulerMarker`/`rulerLength`.

**Declared members:** `orientation: int`, `origin: real`, `pixelsPerUnit: real`, `unit: string`, `majorEvery: real`, `minorEvery: real`, `labels: bool`, `cursorPosition: real`, `minimumMinorSpacing: real`, `markers: var`, `band: var`, `activeBand: var`, `markerSize: real` (ro), `horizontal: bool` (ro).

**Declared signals:** `markerMoved(string id, real position)`, `tabAdded(real position)`, `tabRemoved(string id)`.

**Source methods:** `css()`, `format()`. See source for parameters and helper visibility.

**Object names:** `"rulerBand"`, `"rulerActiveBand"`, `"rulerMarker_" + modelData.id`, `"rulerMarkerArea"`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## Viewport

Scrollable document surface with zoom and coordinate conversion.

**Source:** [src/qml/Viewport.qml](../../../src/qml/Viewport.qml) · **Base:** `Item` · **Kind:** QML component.

zoom/minZoom/maxZoom/fitMode/contentPadding control a single naturally sized child; setZoom/zoomAbout, fitWidth/fitPage, toContent/fromContent and ensureVisible coordinate interaction. Ctrl+wheel zooms, middle drag pans, and ordinary wheel events reach an ancestor Scroll at a pan edge. It does not tile huge documents, rotate the view, render document content or provide selection tools.

**Declared members:** `content: alias`, `contentItem: Item` (ro), `scrollItem: Item` (ro), `zoom: real`, `minZoom: real`, `maxZoom: real`, `fitMode: string`, `contentPadding: real`, `wheelZoomStep: real`, `interactive: bool`, `contentWidth: real` (ro), `contentHeight: real` (ro), `viewportWidth: real` (ro), `viewportHeight: real` (ro).

**Declared signals:** `zoomModified(real zoom)`.

**Source methods:** `clampZoom()`, `contentOriginX()`, `contentOriginY()`, `toContent()`, `fromContent()`, `setZoom()`, `zoomAbout()`, `zoomIn()`, `zoomOut()`, `fitWidth()`, `fitPage()`, `applyFit()`, `ensureVisible()`, `seat()`. See source for parameters and helper visibility.

**Object names:** `"viewportScroll"`, `"viewportSurface"`, `"viewportContent"`, `"viewportWheel"`, `"viewportPan"`.

**Coverage pointers:** [tst_controls_office.qml](../../../tests/qml/tst_controls_office.qml).

## ZoomControl

Minus/readout/plus control for viewport magnification.

**Source:** [src/qml/ZoomControl.qml](../../../src/qml/ZoomControl.qml) · **Base:** `Item` · **Kind:** QML component.

value/from/to/step/presets and zoomIn/zoomOut/reset produce valueModified and resetRequested. The caller synchronizes it with the actual viewport. Compact height 24 px; preset stepping supplies no document coordinate or fit policy by itself.

**Declared members:** `value: real`, `from: real`, `to: real`, `step: real`, `presets: var`.

**Declared signals:** `valueModified(real value)`, `resetRequested()`.

**Source methods:** `clampValue()`, `apply()`, `zoomIn()`, `zoomOut()`, `reset()`. See source for parameters and helper visibility.

**Object names:** `"zoomOut"`, `"zoomReadout"`, `"zoomIn"`.

**Coverage pointers:** [tst_controls_structure.qml](../../../tests/qml/tst_controls_structure.qml).

## StylusHandler

C++ pen and mouse-fallback gesture stream for custom canvases.

**Source:** [src/core/stylus_handler.h](../../../src/core/stylus_handler.h) · **Base:** `QObject` with `QQmlParserStatus` · **Kind:** C++ type.

position/pressure/tiltX/tiltY/rotation/pointerType/buttons expose the latest sample. began/moved/ended deliver stylusSample values; canceled and grabChanged forward the internal handler's state. Mouse pressure defaults 0.6, touch is excluded, synthesized tablet mouse duplicates are rejected. The installed facade uses public Qt headers; its implementation uses Qt Quick private API. It includes no brush engine, smoothing or retained stroke model. Direct C++ construction with a QQuickItem parent attaches immediately; QML construction attaches when the component completes.

**Declared members:** `parent: QQuickItem`, `target: QQuickItem`, `enabled: bool`, `active: bool` (ro), `position: QPointF` (ro), `pressure: qreal` (ro), `tiltX: qreal` (ro), `tiltY: qreal` (ro), `rotation: qreal` (ro), `pointerType: QString` (ro), `buttons: int` (ro), `mousePressure: qreal`, `acceptedDevices`, `acceptedPointerTypes`, `acceptedButtons`, `acceptedModifiers`, `margin`, `grabPermissions`. Former inherited `dragThreshold` and `cursorShape` are not proxied.

**Declared signals:** `began(const QindaTK::StylusSample &sample)`, `moved(const QindaTK::StylusSample &sample)`, `ended(const QindaTK::StylusSample &sample)`, `canceled(const QEventPoint &point)`, `grabChanged(...)`, `sampleChanged()`, `mousePressureChanged()` and property change signals.

**Coverage pointers:** [tst_stylus_handler.cpp](../../../tests/cpp/tst_stylus_handler.cpp).

## stylusSample

QML value type and C++ StylusSample carrying one input sample.

**Source:** [src/core/stylus_handler.h](../../../src/core/stylus_handler.h) · **Base:** `Q_GADGET` · **Kind:** QML value type.

position is parent-local, scenePosition is scene-relative; pressure, tilt, rotation, pointer type, buttons, modifiers, synthesizedFromMouse and timestamp preserve input metadata. Delivered by StylusHandler; it is not an instantiable visual widget or durable document stroke format.

**Declared members:** `position: QPointF`, `scenePosition: QPointF`, `pressure: qreal`, `tiltX: qreal`, `tiltY: qreal`, `rotation: qreal`, `pointerType: QString` (ro), `buttons: int`, `modifiers: int`, `synthesizedFromMouse: bool`, `timestamp: qulonglong`.
