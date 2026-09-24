<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Audio measurement

`AudioMeterModel` is the GUI-side state for a multichannel peak/RMS display.
Its C++ `measureInterleaved` function computes peak sample magnitude and
root-mean-square amplitude independently for each channel in one supplied
interleaved block. Input and output amplitudes use linear full scale: 1.0 is
0 dBFS. The model converts them with `20 log10(amplitude)` and floors the
display at −120 dBFS. `silent` distinguishes zero from a very small signal.
This is sample peak, not intersample true peak; RMS is not LUFS.

`submitLevels(peaks, rms, elapsedMilliseconds)` accepts one pair per channel
on the GUI thread. A pair must come from the same measurement block, so RMS
cannot exceed that block's peak. The caller supplies elapsed wall time between
snapshots. The model applies a configurable peak hold (default 1500 ms) and
display fall (default 24 dB/s) from that elapsed time. A peak at or above
0 dBFS latches `clipped` until `resetClip()`. Channel-count changes reset
meter state. Invalid negative, nonfinite or mismatched level arrays are
rejected without publishing a new snapshot.

`submitInterleaved` is a small fixture/offline convenience path that accepts
up to one million sample values, measures a block and submits it. Nonfinite
samples are ignored and counted in `invalidSamples`. It allocates and is not
an audio-callback API. A real engine must hand measured snapshots from its
realtime callback to a non-realtime worker through a preallocated bounded
handoff, then queue the compact channel levels to this GUI-thread model.
Device timing and that handoff belong to WP-060. Integrated LUFS history and
automation ownership remain WP-061 work.

`AudioParameterMap` converts canonical values to normalized Knob/Fader travel.
Its descriptor names `linear`, `logarithmic` (positive bounds), or `gainDb`
(negative minimum, positive maximum and a unity position inside travel).
For gain, position zero alone means silence; the lowest finite dB value begins
at 0.001, and the configured unity position is exactly 0 dB. The mapper
returns `{value, silent}` so controls never substitute a finite dB number for
negative infinity. Invalid descriptors leave the prior mapping intact.
The host still owns parameter edits and automation transactions.
