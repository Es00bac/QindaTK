<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Audio mixer and routing

WP-062: the mixer of an `AudioSession` (`session.mixer`, `AudioMixerModel`),
the mixing code in `AudioRenderGraph`, per-channel meters, and the four views
over them: `MixerView`, `ChannelStrip` (with `ChannelRoutingPanel`),
`PanningControl` and `RoutingMatrix`. It builds on the engine seams in
[audio-engine.md](audio-engine.md) and the parameter transactions and meters
in [audio-measurement.md](audio-measurement.md). Decisions D-110 to D-113 in
[decisions.md](decisions.md).

```
 tracks (timeline order)        buses (sources first)              master          ports
 track:A ──output──▶ bus:X ──output──────────────────────────▶ master ──output──▶ main ──▶ device 1–2 (or the file)
 track:B ──send (pre/post)──▶ bus:R ──output──▶ master                           device:1-2@<device>
 track:C ──output──▶ "" (disconnected)       every channel: input lanes → fader → meter → panner → routes
```

## Identities

| Thing | ID | Owner |
| --- | --- | --- |
| Track channel | `track:<uuid>` (the timeline track) | `TimelineModel` (name, mute, solo, arm, order) + mixer routing |
| Bus | `bus:<uuid>` | `AudioMixerModel` |
| Master | `master` | `AudioMixerModel`; always present, always stereo |
| Send | `send:<uuid>` | the source channel's routing |
| Group | `group:<uuid>` | `AudioMixerModel` |
| Parameters | `<channel>/gain`, `<channel>/pan`, `<channel>/width`, `<send>/level` | `AutomationModel` lanes (created on first request; never removed) |
| Output ports | `main`, `device:<n>@<device>`, `device:<n>-<n+1>@<device>` | the engine's running stream |

A track channel's gain parameter is the one WP-061 controls already bind
(`AudioRenderGraph::gainParameterId`). `AudioMixerModel.ensureParameter` and
`parameterBinding(owner, kind)` create parameters on request; the binding is
one shared object per parameter, owned by the mixer, so every view and any
control-surface adapter edit and follow the same parameter.

## Laws and layouts (D-111)

Supported channel layouts are **Mono** and **Stereo** (the engine's device
layouts). Gains are in dB with the range minimum as exact silence (D-101):
channel faders and send levels span −60…+12 dB, default 0 dB.

| Pan law (`mixer.panLaw`) | Mono source at pan p (−1 left … +1 right) | Centre |
| --- | --- | --- |
| `-3dB` (default, constant power) | L = cos((1+p)·π/4), R = cos((1−p)·π/4) | 0.7071 each |
| `-4.5dB` | L = √(cos((1+p)·π/4)·(1−p)/2), R likewise | 0.5946 each |
| `-6dB` (linear) | L = (1−p)/2, R = (1+p)/2 | 0.5 each |
| `0dB` (balance) | L = min(1, 1−p), R = min(1, 1+p) | 1 each |

A hard pan is exactly 0 on the far side, and the centre is exactly symmetric.
A stereo source applies width first (mid/side: 0 is mono, 1 as recorded, 2
doubles the side) and then balance (`0dB` row). Conversions without a panner:
a mono file on a stereo channel plays on both lanes; a mono channel takes the
average of the first two file channels; a stereo route into a mono
destination averages; the `main` port on a mono device averages the stereo
master. With the defaults (stereo tracks, centre, width 1, 0 dB) a mono file
reaches the device bit-exactly, as before the mixer.

Every route of a channel — its output and each send — goes through the
channel's panner toward that destination's layout. A post-fader send takes
the fader, a pre-fader send does not; the send level applies to both.

## Routing constraints (D-112)

- Tracks feed buses, the master or a port; they never receive.
- Buses feed buses, the master or a port. The bus graph is acyclic: a route
  or send that would close a loop is refused with the loop, for example
  "Routing B to A would close a feedback loop: B → A → B", and a session file
  with a loop is not loaded (all or nothing, like its other parts).
- The master is always stereo and feeds only a port or nothing; it cannot be
  soloed and is never muted by another channel's solo.
- `main` is the running device's first outputs (or the file of an offline
  render). A device port names its device by ID and plays only while that
  device runs with enough output channels. A route to a port that disappears
  stays as it is, silent and explained (`outputAvailable`, `reason`); nothing
  is reconnected to another port or device by index. A route can only be
  created to a port that is available now.
- Removing a bus disconnects what fed it and removes the sends to it, in one
  undo step, and reports the disconnected channels.

## Mute, solo and groups (D-112)

A muted channel feeds nothing (output and sends) and its meter reads
silence. Solo is solo-in-place: a soloed channel keeps everything that feeds
it and everything it feeds, through outputs and sends, so a soloed track
keeps its bus and its reverb return, and a soloed bus keeps its sources.
Every other channel is implicitly muted (`implicitlyMuted`) unless it is
solo safe. A group's own mute or solo applies to all its members while their
own flags stay unchanged; `effectiveMuted` is the result.

## Operations, history and persistence (D-110)

`AudioMixerModel` operations (`addBus`, `removeBus`, `renameChannel`,
`setLayout`, `setOutput`, `addSend`, `removeSend`, `setSendPreFader`,
`setMuted`, `setSoloed`, `setArmed`, `setSoloSafe`, `setHidden`, `addGroup`,
`removeGroup`, `setGroupMembers`, `setGroupMuted`, `setGroupSoloed`,
`setPanLaw`) answer `{ok, error, …}`. A refused operation changes nothing and
records nothing; the explanation is in `error` and `lastError`.

| Change | History |
| --- | --- |
| Buses, routing, sends, layouts, solo safe, groups, bus/master mute and solo, pan law | the mixer's own undo stack (one step each) |
| Track mute, solo, arm, name | the timeline (the `TimelineTrack` flag `TrackHeader` also edits) |
| Gain, pan, width, send levels | `AutomationModel` (static values and passes, D-100); a pan/width pad gesture is one step (`AudioParameterBinding.commitWith`) |
| Hidden channels | none: view state, saved with the session |

The session saves the mix as the `mixer` part of `qindatk.audio-session/1`
(`schema: qindatk.audio-mixer/1`: `panLaw`, `masterMuted`, `buses`, `strips`
with only non-default routing, `groups`, `hidden`). A session without the
part opens with the default mix.

## Engine

`AudioRenderGraph::compile` orders the channels (audio tracks in timeline
order, buses after every bus that feeds them, the master last), resolves
audibility, parameter curves, routes and ports, and sizes the graph's own
work memory (`kChunk` = 256 frames per lane). `render()` mixes in chunks,
one pass in that order, so a destination is always complete before it
plays. Nothing is allocated, locked or waited for on the device thread;
`tst_audio_mixer_engine` counts heap calls while routes, mutes and faders
change during playback. Mixer changes recompile the graph on the GUI thread
and the engine adopts it at the next period boundary, so a route change
never produces a partly switched period. Parameter changes take effect per
period without a ramp (no de-zippering).

An offline render (`renderToFile`) is the `main` output: the master mix
whatever the master's device routing, with device ports silent. A device
render writes each port to its device channels.

## Channel meters (D-113)

`AudioChannelMeterSink` (one per running stream) receives every channel's
post-fader lanes from `render()` and closes a frame every 10 ms (the output
meter's block): peak and sum of squares per lane for up to 128 channels. A
frame is closed early when the graph generation changes, so its slots always
belong to one graph. The GUI thread drains the bounded ring on each poll
into `AudioEngine.channelMeter(channelId)` (`AudioMeterModel`, the WP-061
ballistics on audio time; `mixer.meter(channelId)` returns the same model).
A channel that leaves the graph, or a stopped stream, leaves its meter with
no channels.

## Views

| View | Model | Notes |
| --- | --- | --- |
| `MixerView` | `mixer.strips` (`AudioMixerStripModel`: rows move, never reset) | virtualized `ListView`; a bank is the whole strips that fit, stretched to fill the width, and scrolling snaps to strip edges; compact/detail strips (only the fader gives up height in a short strip); group bar; Left/Right, Home/End, Page Up/Down; while a gesture holds a channel, bank changes wait, the held strip stays in view, and a hidden held channel keeps its strip until release |
| `ChannelStrip` | row roles + `mixer` | meter (`AudioMeter` over the channel meter), `Fader` (shared gain binding), `PanningControl`, mute/solo/arm/solo safe/hide, groups, `ChannelRoutingPanel` in detail mode; host mode keeps the request protocol |
| `ChannelRoutingPanel` | `mixer.destinations(id)`, row `sends` | output selector with refusal explanations; sends with level `Knob`, pre/post toggle and removal; add send |
| `PanningControl` | pan and width bindings | canonical pan −1…+1 and width 0…2 at 0.01; sliders, percent fields and a position × width pad share the transactions |
| `RoutingMatrix` | `routingSources`, `routingDestinations`, `connections` | each row is a source with its destination; unavailable connections stay shown; refusals are explained |

`Fader` and `Knob` keep the binding a gesture began on: a recycled delegate
cancels an open gesture on the old binding instead of committing into
another channel's parameter.

## Verification (run by the round build)

| Test | Checks |
| --- | --- |
| `tst_audio_mixer` | identities, defaults and parameter descriptors; every refusal and its explanation; undo/redo; save/reopen gives the same mix and samples; a looped file is refused whole; strip rows move without resets and keep a held hidden channel; device ports stay unavailable and silent while an offline render is the master mix |
| `tst_audio_mixer_dsp` | all four pan laws at known positions; width and balance; buses, post/pre-fader sends and faders; mute, solo in place, solo safe, groups and master mute; pan and send-level automation per sample; the pad's two-parameter gesture as one undo step |
| `tst_audio_mixer_engine` | on the loopback device a route change during playback switches at period boundaries with every sample either the source or silence; channel meters measure known post-fader levels; zero device-thread allocations during route, mute and fader changes; a device port never falls back to another device |
| `tst_mixer_routing.qml` | MixerView creates fewer than 20 strips for 41 channels and keeps a gesture through a bank change, hiding and a reorder; a strip key step is one engine transaction and another writer moves its fader; matrix and routing panel explain refusals; pad, field and keyboard give canonical pan and width |

Not covered here: plugin inserts and `PluginRack` (WP-066); surround layouts
beyond stereo; control-surface I/O (the protocol adapter writes through
`parameterBinding`); parameter de-zippering; performance measurement,
screen-reader runs, other platforms and output-fidelity comparison
(WP-088…WP-094).
