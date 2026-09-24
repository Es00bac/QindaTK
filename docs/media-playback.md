<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Media decoding, playback and export

QindaTK's media stack decodes local audio/video files with exact frame
identity, plays a source clip or a `TimelineModel` program through the shared
transport, shows frames on the GPU or software scene graph, organizes media in
virtual bins with proxies and relinking, and exports a program to a verified
file. It is the backend behind `SourceProgramMonitor` and `MediaBin`.

```
MediaCatalog ── ResourceResolver ── MediaDecoder (FFmpeg libraries, optional)
      │                                   │ exact index, seek, audio
MediaBin (QML)                      MediaProgram (source | timeline snapshot)
                                          │ render(frame) · mixAudio(samples)
MediaPlayer ── MediaPlaybackEngine (TransportAdapter) ── MediaDecodeWorker
      │ transport (TransportModel)        │ frame cache, prefetch, cancellation
SourceProgramMonitor ── MediaFrameView (texture node)
MediaExport ── MediaEncoder (FFV1 + PCM in Matroska) ── RenderQueueModel provider
MediaFrameFeed ── MotionFrameRenderer sources · NodeGraphModel source nodes
```

## Backend (D-096)

The decoder, encoder and scaler are the FFmpeg libraries (`libavformat`,
`libavcodec`, `libavutil`, `libswscale`, `libswresample`), found by
`pkg-config` at configure time and linked privately into `qindatk`
(`QINDATK_ENABLE_FFMPEG`, default ON). Their headers stay in
`media_ffmpeg_p.h` and the `.cpp` files, so consumers never need them. A build
without them still has every type; each operation then reports
`ErrorCode::Unsupported` ("built without the FFmpeg media backend"), and
`MediaDecoder::available()` / `MediaEncoder::available()` say so up front. The
asset backend (`FileAssetBackend` video `metadata`/`thumbnail`/`frame`
requests) uses the libraries when present and the FFmpeg command-line tools
otherwise.

## Frame identity and time (D-097)

A source frame is identified by **(resource ID, content revision, zero-based
presentation index)**. Opening a file indexes every video packet once (no
decoding) and sorts it into presentation order. The index is cached per
process (64 files, keyed by path, size and modification time).

Rate policy, chosen from the container's declared rates:

* **Constant rate**: when every timestamp equals `i / rate` within one unit of
  the stream time base (this absorbs Matroska's millisecond rounding), frame
  *i* starts at exactly `i / nominalRate` seconds. A 24000/1001 frame 1 starts
  at 29,429,400 ticks of the default 705,600,000 ticks-per-second timebase.
* **Variable rate**: otherwise frame *i* starts at `pts[i] − pts[0]` in the
  stream time base; the last frame lasts its packet duration (or the previous
  interval).

Every conversion rounds once. A frame starts at the first tick at or after its
exact start (ceil); a tick belongs to the last frame that has started (hold),
so `frameAtTicks(frameStartTicks(i)) == i`. Times outside `[0, duration)` have
no frame. Duplicate timestamps are dropped and reported as damage; a file that
ends before its declared duration is `truncated`; packets without timestamps
are not indexed and are reported.

Colour policy (`MediaSourceInfo::frameColorSpace`): SDR Rec.601/709 and
untagged video is tagged encoded sRGB (a preview approximation, policy
`srgb-approximation`); BT.2020/P3 primaries and PQ, HLG or linear transfer keep
their own `QColorSpace` tag (`wide-gamut-*`, `hdr-pq`, `hdr-hlg`, `linear`) so a
consumer converts explicitly. YUV→RGB uses the stream's matrix and range.
Grading and linear-light pipelines belong to WP-072.

## Audio sync

Media time zero is the first video frame. Audio sample positions are stated
relative to it: `audioOffsetSamples` places the stream (a stream that starts
100 ms late at 48 kHz has offset 4800). `readAudio(first, count, rate,
channels)` returns interleaved float for media samples `[first, first +
count)`; silence stands where the file has no audio. Sample positions are
exact for PCM and for streams whose time base is the sample rate; mono feeds
every output channel at unity gain, N→1 averages. Other rates go through
libswresample.

A program's transport position is a sample count at the program sample rate.
The frame on screen is `frameAtSample(position)` and the audio that belongs
to it is `mixAudio(position, n)`, so picture and sound share one position.
Export writes each frame's exact sample span `[frameStartSample(f),
frameStartSample(f + 1))`, which keeps a flash frame and its tone aligned in
the output (verified by the test signal's sync pops). The playback clock is
`MediaPlaybackClock`: `MediaSystemClock` for preview, `MediaManualClock` for
tests and offline stepping. Audible device output belongs to the audio engine
(WP-060, [audio-engine.md](audio-engine.md)): an adapter reads `mixAudio` ahead of the
device callback on its own thread and supplies a clock from the device's
played-sample count, which makes the device the master and video follows it.
That adapter is not part of WP-070.

## Decoder

`MediaDecoder` is used by one thread at a time. `decodeFrame(index, maxSize,
cancel)` returns the frame whose timestamp is the indexed one, fitted inside
`maxSize` (never upscaled), as `Format_RGB32` (or straight-alpha
`Format_ARGB32` for alpha codecs) with the colour tag above. Short forward
steps (up to 96 frames) continue decoding; other requests seek to the key frame
at or before the target, then an earlier key frame, then the start, and as a
last resort reopen the file and decode from the first packet. Damaged packets
are skipped; a target that cannot be decoded is an error, never a neighbour.

Cancellation is cooperative and prompt: the token is checked between packets
and inside libavformat I/O (interrupt callback). A cancelled call returns
`ErrorCode::Cancelled` and the decoder reopens its demuxer lazily, so it stays
usable. `MediaDecoderPool` keeps up to eight open decoders per thread and
reopens a file whose size or modification time changed. Limits: 8192 pixels
per side, 10,000,000 indexed frames, 60 seconds of audio per read.

## Programs

`MediaProgram` is an immutable snapshot shared by worker threads.

* **Source program** (`forSource`): one file frame by frame; its frames are
  the source's presentation indices. A proxy may replace the pictures; frame
  identity and audio always come from the original.
* **Timeline program** (`fromTimeline`): a `TimelineModel` revision at its
  `TimeMap` frame and sample rate. Program frame *f* starts at `origin +
  ceil(f · timebase / frameRate)`. Video tracks draw from the last track to the
  first (the first track is in front), hidden tracks are skipped, and each
  clip maps program ticks *T* to source ticks `sourceOffset + (T − start)`,
  held to a source frame (so 23.976 sources conform to a 25 fps program).
  Sources are letterboxed at their display aspect. Audio mixes every unmuted
  audio track (only soloed tracks when any is soloed), placed at the first
  sample at or after the clip start. Unresolved resources are listed in
  `unresolved`; their clips render as missing (`missingResourceIds`), and a
  source time outside the media is reported in `outOfRangeClipIds`, never
  drawn from a neighbouring frame.

## Playback engine (D-098)

`MediaPlaybackEngine` implements `TransportAdapter`; `TransportModel` is its
GUI projection. Play, stop, seek and loop are acknowledged with the resulting
snapshot on the next event-loop turn, so the caller always holds the request ID
first. Seeks outside `[0, end]` are refused; record and punch are unavailable.
While playing, an 8 ms precise timer reads the clock; reaching the end stops
on the last frame, and play at the end restarts from frame 0.

The target frame is `frameAtSample(position)`. The engine requests it at
presentation priority and, while playing, prefetches the next six frames on
its decode thread. Only a result for the current target and program revision
is presented; anything else is cached or counted `stale`, and superseded
queued or running work is cancelled through its token. So rapid seeks may
leave the view pending but never show a superseded frame. A target that fails
(offline or damaged media) is presented as a failure with no picture, and a
program that becomes empty clears the display. Frames the clock skipped are
counted `dropped`. The frame cache holds 48 frames (0–256) and at most 256 MiB
per engine; `statistics` exposes requested/presented/stale/cancelled/failed/
dropped/cacheHits.

## Frame transfer

`MediaFrameView` keeps one implicitly shared copy of the presented frame. In
`updatePaintNode` it creates a texture from it only when a new frame was
presented, and the `QSGImageNode` owns that texture and deletes the one it
replaces. The RHI adaptation uploads a GPU texture; the software adaptation
keeps the image (`frameTransfer` reports `gpu` or `software`). `displayedFrame`
names the frame whose pixels are on screen.

## MediaPlayer and the monitor

`MediaPlayer` is the QML face of one engine. Source mode: `resourceId` (and
optionally `resolver`, `catalog`); program mode: `timeline`, `frameWidth`,
`frameHeight`, `audioChannels`. It rebuilds on resolver relinks, catalog proxy
changes and committed timeline revisions. `state` is `empty`, `loading`,
`ready`, `missing` or `failed`. `presentedFrame`, `presentedTicks`,
`presentedFrameKey` (`resource@revision#frame`), `presentedTimecode`,
`presentedSources` and `missingResourceIds` describe the frame on screen;
`targetFrame` and `pending` describe where the transport is. `seekFrame`,
`stepFrame`, `seekTicks` (the frame showing at those ticks), `play`, `stop` and
`togglePlayback` return transport request IDs; `exportJob(outputPath)` returns
a render-queue snapshot of the timeline program from original media.

`SourceProgramMonitor` with `player` set shows a `MediaFrameView`, seeks through
the player, closes each seek on the engine's `commandFinished`, and shows the
presented identity (timecode · frame · ticks). Keys: Space play/stop, Left/Right
step, Home/End first/last frame. Without `player` it keeps the host-owned
protocol.

## Media catalog (D-099)

`MediaCatalog` is the media bin's model. `importMedia(uri, binId, label)`
registers a resolver resource and adds an asset; `addAsset` wraps a resource
the host registered. Metadata is read by a `JobModel` job (`metadataState`
`pending`/`ready`/`missing`/`failed`). Bins are virtual: `addBin`, `renameBin`,
`removeBin` (assets move to the root) and `moveToBin` change organization only;
the `resourceId` sequences reference never changes. Rows are the assets
matching `binFilter` and `searchText` (label, file name, tags, codec, source
timecode). `relink(assetId, uri)` changes the resolver location (a different
file is accepted and inspectable as `modified`); `relinkMissingFrom(folder)`
relinks offline assets only to same-named files whose content hash matches.
`createProxy(assetId, maxHeight)` writes an FFV1/PCM proxy with the original's
frame count and rate (constant-rate sources only), registers it as its own
resource and sets `proxyStatus` `creating`→`ready`; `setProxyActive` switches
previews per asset, and exports always read originals. `contentMap()` /
`loadContent()` persist bins, labels, tags and proxy links
(`qindatk.media-catalog/1`).

## Frame feeds

`MediaFrameFeed` fills `MotionFrameRenderer` layer sources and `NodeGraphModel`
source nodes from media. `prepare(ticks)` evaluates the composition, decodes
each visible media layer at its exact `localTicks` (and each graph source at
`ticks`, with `ticksPerSecondNumerator/Denominator`), then sets every source
in one step and emits `prepared(requestId, ticks)`; the host renders after it.
Sources hold their first/last frame outside the media. A newer `prepare`
cancels the older one, whose results never apply. Offline media removes the
layer source (`removeSource`) or clears the node (`clearSourceImage`), and a
relinked resource re-prepares the last prepared time.

## Export and the test signal

`MediaExport::exportProgram(program, path, first, count)` renders the same
frames and audio mix that playback presents into lossless FFV1 (8-bit RGB) and
PCM s16le in Matroska; missing media fails the export and cancellation removes
the partial file. Every `RenderQueueModel` registers the
`qindatk.media-program-mkv` provider (format `mkv`) when the encoder is
available; `MediaExport::queueSnapshot` (or `MediaPlayer.exportJob`) captures
the program JSON and each source's SHA-256, and the provider re-hashes every
source before encoding, so a changed or offline source fails the job.

`MediaCatalog.generateTestSignal(uri, options)` (C++
`MediaExport::writeTestSignal`) writes colour bars, a 16-cell binary frame code
in the bottom strip (`readTestSignalCode`) and a white flash with a 1 kHz tone
over the exact sample span of every `popInterval`-th frame, starting at frame
`popPhase` (default 0). A nonzero phase keeps frame 0, the frame monitors and
bin thumbnails show first, on bars. Tests and the
`examples/media-source-browser.qml` fixture use it as known-answer media.

## Limits and ownership

Local files only (remote URLs are unsupported). Proxies need constant-rate
sources; export of a variable-rate source goes through a timeline program,
which conforms it. Performance budgets, other platforms and Qt versions,
screen-reader runs and output-fidelity comparison are qualification work in
WP-088 to WP-092 and WP-089. Trim/roll/slip edits are WP-071; grading, scopes
and captions are WP-072.
