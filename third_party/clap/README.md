<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# CLAP headers (vendored)

`include/clap/` is the unmodified public header set of the CLAP audio plugin
ABI, release **1.2.9** (tag `1.2.9`, commit
`cd94482ba5941ae410809b6fbaed3bc851044270`) from
https://github.com/free-audio/clap. It is MIT licensed (`LICENSE`, and
`THIRD_PARTY_NOTICES.md` at the repository root).

The headers are header-only C declarations. QindaTK uses them in two places
only: the out-of-process plugin host executable (`src/pluginhost/`) and the
test plugins (`tests/plugins/`). The `qindatk` library itself never includes
them (decision D-190, docs/audio-plugins.md).

To update: replace `include/` with the `include/` directory of a newer CLAP
release, keep the files unmodified, and update the version above and in the
notices. The files keep their upstream headers (no SPDX line of ours), and
AGENTS.md's 500-line rule does not apply to them.
