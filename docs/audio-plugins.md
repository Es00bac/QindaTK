<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Audio plugin hosting

WP-066: real plugins in the mixer's insert chains. `AudioSession.plugins`
(`AudioPluginHost`) owns each channel's chain; `AudioPluginRegistry` finds
the installed plugins; every instance runs in its own
`qindatk-plugin-host` process; the session's `AudioRenderGraph` processes
the inserts and compensates their latency. `PluginBrowser` and `PluginRack`
are the views. Decisions D-190 to D-193 in [decisions.md](decisions.md).
It builds on [audio-engine.md](audio-engine.md) (graph, threads, offline
render), [audio-mixer.md](audio-mixer.md) (channels, routes) and
[audio-midi.md](audio-midi.md) (notes for instruments).

```
 GUI thread (qindatk)                                   qindatk-plugin-host (one per instance)
 AudioPluginRegistry ── --scan <binary.clap> ─────────▶ dlopen, clap_entry, factory → JSON, exit
 AudioPluginHost                                         main thread: Qt event loop
   AudioPluginInstance ── JSON lines (stdin/stdout) ───▶   load · activate · params · set-params
     QProcess, requests with a time limit                  state-save/-load · gui-open/-close · quit
     events ◀── param/gesture/restart/gui-resize ──────   timers + fds for plugin editors
   AudioPluginEditorWindow ◀── x11 child window ───────   plugin editor (embedded)
 device / offline render thread                          audio thread
   AudioRenderGraph::runInserts                            futex wait on request
     AudioPluginProcessor ── shared block (memfd) ─────▶   plugin->process(block)
       futex wake request, futex wait response ◀────────   response = request, futex wake
```

## Formats and capabilities (declared by `AudioPlugins::formats()`)

| Format | State | Notes |
| --- | --- | --- |
| CLAP 1.2.9 | available (needs `qindatk-plugin-host`) | Headers vendored under `third_party/clap` (MIT); used only by the host executable and the test plugins |
| LV2 | not available | No adapter: lilv is not installed here and is not a dependency |
| VST3 | not available | No adapter: the SDK is not present |

`AudioPluginHost.formats` and `AudioPluginRegistry.formats` return one map
per format (`format`, `available`, `reason`, `isolation`, `audio`, `notes`,
`parameters`, `state`, `latency`, `editor`, `transport`). Evidence is kept per
format: only CLAP is advertised as available.

The host executable is found in this order: `$QINDATK_PLUGIN_HOST`; the
build tree's `libexec/qindatk-plugin-host`, or an install's
`<libexecdir>/qindatk/qindatk-plugin-host`, both located relative to
`libqindatk` itself so a moved install keeps working (D-202); then `PATH`.

What the CLAP host supports:

| Area | Supported | Not supported |
| --- | --- | --- |
| Audio | the main input and output port, mono or stereo, 32-bit float; blocks of at most 256 frames (`AudioRenderGraph::kChunk`); other ports get silent scratch buffers | 64-bit processing, side-chains, surround layouts |
| Notes | CLAP note on/off (from MIDI track patterns and launched clips) to the first instrument of a MIDI track; a transport jump ends held notes | MIDI CC, per-note expression, MPE, note output |
| Parameters | every automatable, visible, writable parameter as an automation parameter; values sent at block starts; editor gestures back as automation transactions | sample-accurate automation inside a block, modulation (`PARAM_MOD`), per-note parameters |
| State | `clap.state` save/load as an opaque blob in the session | `state-context`, presets |
| Latency | `clap.latency`, read after every activation; `request_restart` reactivates | tail reporting |
| Editor | `clap.gui` x11 embedded (xcb, offscreen, minimal) or floating (elsewhere); resize requests; timer and fd support for the editor's event loop | editor scaling beyond the window's device pixel ratio, resize hints |
| Host extensions | log, thread-check, params, state, latency, audio-ports, note-ports, gui, timer-support, posix-fd-support | transport, track-info, thread-pool, voice-info |

## Discovery and cache

`AudioPluginRegistry.searchPaths` defaults to the CLAP convention on Linux:
`$CLAP_PATH` entries, `~/.clap`, `/usr/lib/clap`, `/usr/lib64/clap`,
`/usr/local/lib/clap`, searched recursively for `*.clap` files. Each binary
is scanned by `qindatk-plugin-host --scan <binary>` in its own process,
with `scanTimeoutMs` (10 s) as the limit:

| Result | Binary row `state` | Browser |
| --- | --- | --- |
| Scanned | `ok` | one row per plugin; a plugin whose `init()` fails is listed unavailable with the reason |
| Refused to load, not CLAP, no factory | `failed` | one unavailable row for the file, with the reason |
| Crashed while scanned | `crashed` | one unavailable row |
| Did not finish in time | `timeout` | one unavailable row |

Results are cached (`cacheFile`, `qindatk.plugin-cache/1`) by canonical
path, size and modification time: an unchanged binary, including one that
failed, is not scanned again until `scan(true)`/`scanNow(true)`. `scan()` is
asynchronous (a worker thread runs the scan processes); `scanNow()` blocks.
The first binary in search-path order that provides a plugin ID wins.
**A session never names a binary to load**: an instance is started only
from a plugin the registry found (D-190).

## Instances, chains and lifecycle

`addPlugin(channelId, pluginId, index)` starts an instance
(`plugin:<uuid>`) on a mixer channel (`track:<uuid>`, `bus:<uuid>`,
`master`) and answers `{ok, error, instanceId}`; a failed add leaves nothing.
`removePlugin`, `movePlugin(instanceId, index, channelId)`,
`setBypassed`, `reload`, `openEditor`, `closeEditor` answer the same way.
Starting an instance:

1. `AudioPluginProcessor` creates the shared block (`memfd`, close-on-exec).
2. `qindatk-plugin-host --serve <binary> <plugin id> --fd <n>` starts; only
   this child inherits the block's descriptor.
3. `load` (entry, factory, `create_plugin`, `init`, extensions, ports) → the
   saved state (`state-load`), if any → `params` → `activate` at the session
   rate with 256 frames maximum → the processor is configured and handed to
   the device.

| State | Meaning | Audio |
| --- | --- | --- |
| `ready` (`bypassed` when bypassed) | running | processed (bypassed: dry, delayed by its latency) |
| `failed` | crashed, hung, refused a request, or a request timed out; `error` says which | dry, delayed by its last latency |
| `missing` | the plugin is not installed here | dry |
| `unloaded` | the host is shutting down | dry |

Every control request has `requestTimeoutMs` (5 s). A failed or missing
instance keeps its ID, slot, bypass flag, parameter IDs and saved state;
`reload(instanceId)` starts it again from the registry with that state.
`AudioPluginHost.instanceFailed(instanceId, message)` reports each failure.

## Realtime exchange and failure containment (D-190, D-192)

Per 256-frame chunk and insert, the rendering thread writes the input lanes
and events into the block, publishes `request = n`, wakes the host's audio
thread (futex) and waits for `response = n` for at most `realtimeTimeoutMs`
(50 ms) on the device, `offlineTimeoutMs` (5 s) in an offline render. The
wait is a futex wait in slices of 5 ms with a zero-timeout `poll` on the
host's pidfd between slices, so a crash is noticed within milliseconds. No
allocation, lock or I/O happens on the device thread (`tst_audio_plugin_engine`
counts heap calls).

| Situation | What the insert plays | Then |
| --- | --- | --- |
| Answer in time | the plugin's output | — |
| Late answer | dry, and no new block is sent until the late one is answered | after `hangTimeoutMs` (2 s) of audio time without an answer the processor is hung; the host kills the process: `failed`, "stopped responding" |
| Late answer in an offline render | dry for the rest of the render | `failed`, "stopped responding" |
| Host process died | dry | `failed`, "crashed" |
| Bypassed, missing, failed | dry, delayed by the latency | — |

A plugin can therefore cost one late period when it hangs, never the
session: the device keeps running and every other channel keeps playing.

## Parameters and automation (D-191)

Each automatable, visible, writable parameter with a finite range becomes
the automation parameter `<instance id>/<CLAP id>`: range = the plugin's
plain range, unit `plain`, step 1 and step interpolation for stepped
parameters, default = the plugin's value when the instance was added.
Read-only, hidden and bypass parameters are not mapped (the host bypasses
by itself). `parameterBinding(instanceId, clapId)` hands out one shared
`AudioParameterBinding` per parameter, so a `Knob`, an automation lane and
undo behave exactly like the mixer's controls.

- **The automation is the source of truth.** The graph evaluates each
  mapped parameter at the start of every block and sends a
  `CLAP_EVENT_PARAM_VALUE` when it differs from the last value sent. After a
  reactivation or reload every value is sent again, so a recalled session
  plays its automation, whatever the plugin's state held.
- While nothing plays, changes reach the plugin through a params flush in
  its process (`set-params`), so an open editor and a saved state follow the
  knobs.
- A change made in the plugin's own editor (output events, with gestures)
  becomes one automation transaction through the binding: gesture begin,
  previews, commit — one history step, recorded when a pass writes.
- A parameter ID stays when an instance is moved, bypassed, fails, is saved
  and reopened, or is reloaded. Automation parameters are never removed
  (removing one clears automation history), like the mixer's (D-110).

## Latency compensation (D-192)

Inserts run pre-fader in slot order. `AudioRenderGraph::compilePlugins`
adds each channel's insert latency (reported by `clap.latency` after
activation) and, in processing order, computes when each channel's output
arrives; every route into a bus, the master or a port waits (a delay line
per route) for the latest route into it. All paths therefore meet aligned,
the master is late by `graph->pluginLatency()` frames, and:

- a device hears the mix `pluginLatency` frames late (no look-ahead);
- `renderToFile` renders that far past the end and drops as many leading
  frames, so a file lines up with the timeline;
- a bypassed or failed insert passes its input delayed by its latency, so
  bypass never shifts the mix;
- a plugin whose latency changes asks for a restart; the host reactivates
  it and recompiles the graph (delay lines restart from silence).

`insertLatency(channelId)` and `compensation(channelId)` report both sides.

## Offline renders

`AudioSession.renderToFile` holds an `AudioPluginOfflineClaim` while it
renders: every live instance is claimed away from the device (the device
passes its input through unchanged meanwhile, without touching the
processor's delay line, which the owner may be reallocating), reactivated at the render's rate when that
differs, and reset before and after, so a render is deterministic and its
tails do not leak into playback. Renders run the same `render()` code as
the device.

## Editors (D-193)

`openEditor(instanceId, ownerWindow)`: under xcb (and the offscreen and
minimal platforms, where tests run) the toolkit creates an
`AudioPluginEditorWindow` (object name `pluginEditorWindow`), transient for
`ownerWindow`, and passes its window ID to the plugin (`clap.gui` x11, not
floating: `create`, `set_scale`, `get_size`, `set_parent`, `show`). The
toolkit owns the window: title, position, transient parent, closing and
lifetime; the plugin owns its child window and that child's keyboard focus.
`request_resize` resizes the window (fixed size unless the plugin can
resize). Closing the window closes the editor first (`hide`, `destroy`) and
then deletes the window. Elsewhere (Wayland) the plugin shows a floating
window of its own. A plugin that fails takes its editor window with it.

## Session persistence

The session saves a `plugins` part (`qindatk.audio-plugins/1`): `chains`,
each `{channelId, inserts}`, each insert `{instanceId, format, pluginId,
name, vendor, version, bypassed, state (base64), parameters}`, with
`parameters` = `{id, name, module, min, max, default, stepped, mapped}`.
Saving asks every live plugin for its state first. The part is validated
with the other parts (all or nothing) and loaded last, after the
automation it refers to. A session without the part has no plugins. A
missing plugin is saved again unchanged.

## Views

| View | Host side |
| --- | --- |
| `PluginBrowser` | `plugins: session.plugins.browserPlugins` (registry rows plus missing session plugins), `scanState`/`scanMessage` from `session.plugins.registry`; `addRequested` → `rackAction("add", "", {channelId, pluginId})` → `acknowledgeAdd` |
| `PluginRack` | `instances: session.plugins.revision, session.plugins.instances(channelId)`; `pluginActionRequested(requestId, instanceId, action, payload)` → `rackAction(action, instanceId, payload, Window.window)` → `acknowledgeAction(requestId, result.state, result.message)` |

Rack rows carry `binding` per parameter (drawn as a `Knob`) and a failed or
missing instance shows **Reload** (`pluginReload_<instanceId>`).

## Verification (run by the round build)

| Test | Checks |
| --- | --- |
| `tst_audio_plugin_host` | scan and cache of the test binary; a binary that crashes while scanned and a non-plugin file stay listed with reasons; capability declaration per format; lifecycle (process, parameters as automation, a knob edit reaching the plugin's state, removal ends the process); a latency change restarts the plugin; `init` failure and an activation time-out leave nothing; a crash inside an offline render is contained (dry audio) and reloaded with its state; a hang inside an offline render is cut; the editor lifecycle (embedding, resize, an editor change as an automation transaction, close order, a crash with the editor open); session recall with the plugin installed, missing (identity and state kept, saved again unchanged) and installed later; shutdown ends every process cleanly |
| `tst_audio_plugin_engine` | processed samples exact (gain, bypass); latency compensation across two tracks and under bypass, with a file aligned to the timeline; an instrument's notes on their exact samples; no device-thread allocation with an effect and an instrument; a crash and a hang during playback leave the session playing; unloading while the engine plays |
| `tst_plugin_host_controls.qml` | parameter rows with a binding draw a Knob; a failed instance offers Reload |

The test plugins (`tests/plugins`, built as `qindatk-test.clap` and
`qindatk-test-broken.clap`) are real CLAP binaries: `org.qindatk.test.gain`
(gain, latency with restart, read-only block counter in its state, an x11
editor that asks to be resized and makes one edit), `.synth` (level ×
velocity per held note), `.faulty` (crashes or hangs in `process()`),
`.init-fails` and `.slow` (activation takes 30 s).

## Not covered here

LV2 and VST3 adapters (no SDK here; each would add its own evidence);
side-chains, surround and 64-bit ports; transport information for plugins;
sample-accurate parameter automation inside a block; plugin presets
(`PluginBrowser` preset preview stays a host request); a shared host process
for several instances; performance measurement, other platforms (Windows,
macOS window embedding), screen-reader runs and translation (WP-088 to
WP-094).
