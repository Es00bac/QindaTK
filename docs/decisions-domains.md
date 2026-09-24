<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Domain decisions

Continuation of [decisions.md](decisions.md) for specialized domain packages
(scientific imaging and similar); [application decisions](decisions-application.md)
and [service decisions](decisions-services.md) hold the others.

## D-086 — Voxel indices are cell centres; slices show whole cells

**Context.** The first slice view fitted its image to size × spacing but
mapped pixels between the outer voxel centres, so edge voxels drew at half
width and the in-plane aspect drifted by up to one voxel per axis (2.5% for a
40-slice, 3 mm coronal plane). Sampling, clicks and the crosshair need one
convention shared with future volume rendering.

**Decision.** A voxel index is the centre of a cell one spacing wide, as in
ITK and NIfTI. A volume samples continuous indices within −0.5 … size − 0.5;
`Nearest` picks the containing cell and `Linear` clamps to the outer centres
inside the edge half cell. A slice rectangle spans whole cells, and
`sliceImage`, `voxelAt`, `positionForVoxel` and the crosshair share that one
mapping. The cursor stays within the outer centres.

**Consequences.** Every voxel draws at its physical size and a click inside
an edge cell selects that voxel. World positions are unchanged (`world =
origin + orientation × index × spacing`). Views stay in voxel-index planes;
oblique or patient-axis reslicing needs its own resampling contract.

## D-088 — A queue-driven realtime engine with built-in silent devices and optional ALSA

**Context.** WP-060 needs a functional audio engine: real device I/O,
recording, offline render and measured callbacks, without making core QindaTK
depend on an audio runtime, and with automated checks that never play sound.
The machine offers ALSA 1.2, PipeWire 1.6, PulseAudio and Qt Multimedia.
PipeWire needs a running server and silent tests would add nodes to the
user's live graph; Qt Multimedia hides the callback and has no null device;
ALSA runs without a server, reaches PipeWire through its `default` PCM, and
its null/file plugins give real alsa-lib I/O into files.

**Decision.** The engine core (`AudioEngine`, `AudioEngineProcessor`,
`AudioRenderGraph`, `AudioSession`) is always built with two software
reference devices, `builtin:null` and `builtin:loopback`, paced by the
monotonic clock. ALSA is an optional backend (`QINDATK_ENABLE_ALSA`, used
only when `libasound` links), linked privately like Poppler. The device
thread only runs `process()`; it talks to the GUI through bounded lock-free
SPSC rings and atomics, adopts immutable graphs by pointer and never frees
them; devices open and close on a worker thread. Media decode is RIFF/WAVE in
core; the session reuses `TimelineModel`, `AutomationModel`/`EngineSchedule`
and `ResourceResolver` rather than defining its own edit, time or media
identity model.

**Consequences.** Core consumers build and test without any audio library;
with ALSA, `libasound` becomes a runtime dependency of `libqindatk`. The same
render function feeds devices and files, so offline output is the reference
for device output. Other platform backends (PipeWire native, JACK, WASAPI,
CoreAudio) implement `AudioDeviceBackend` and are qualified in WP-088; they
change no engine, session or QML contract.
