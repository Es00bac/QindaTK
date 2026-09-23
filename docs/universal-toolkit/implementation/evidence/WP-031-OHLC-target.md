<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-031 OHLC and target display lane evidence

Isolated worktree from integration `2afec80`, 2026-09-23. Linux offscreen
software renderer, Qt runtime 6.11.1, GCC 15.3.0, Debug/Ninja.
`portageq envvar MAKEOPTS` returned `-j24 -l24`. The build used plain
`cmake --build build/wp031-financial`, then a named-target incremental build;
no job/load override was applied.

## Focused checks

- `tst_financial_models`: pass. A three-slot OHLC fixture has observed price
  domain [95,112], exact UTC millisecond strings, one explicit missing slot,
  and a reference at 100. The target fixture has range [0,100], current 82,
  target 95, and named bands [0,40], [40,70], [70,100]. Invalid high/low,
  invalid time/price data, out-of-range target/current and incomplete bands
  reject replacement without changing the prior generation. Page calls
  reject requests above the 512-slot rendering cap.
- Focused `tests/qml/tst_financial_displays.qml`: 4 pass, 0 fail. Synthetic
  mouse clicks select candles, bullets and gauges by stable ID; assigning IDs
  or replacing/reordering rows remains silent. Missing values, data-table
  visibility, model detach and CSV are checked.
- `example_financial-displays` and `example_gallery`: pass.
- `qmllint` using the CMake module import and resource paths for the two new
  QML files: exit 0, no warnings.

## Visual evidence

The [compact](WP-031-OHLC-target-compact.png),
[comfortable](WP-031-OHLC-target-comfortable.png), and
[table](WP-031-OHLC-target-table.png) PNGs were opened and inspected. The
OHLC plot shows outlined rising and filled falling candles, an explicit
missing cross, and the reference at 100. Bullet and gauge views show distinct
current and target marks; the missing capacity measure has no current mark.
The table screenshot shows the exact source UTC strings and missing row.

Representative `qtk-preview --dump` lines at 1180×840:

```text
compact:     CandlestickView#candlestickView  8,32 1164x264
compact:       CanvasItem#candleCanvas  0,0 1164x209
compact:     TargetDisplay#targetDisplay  8,300 1164x264
compact:       CanvasItem#targetCanvas  0,0 1164x209
comfortable: CandlestickView#candlestickView  10,35 1160x262
comfortable:   CanvasItem#candleCanvas  0,0 1160x200
comfortable: TargetDisplay#targetDisplay  10,302 1160x262
comfortable:   CanvasItem#targetCanvas  0,0 1160x200
table:       DataTable#candleDataTable  0,0 1164x209
```

## Boundary and remaining work

The host owns market calendar construction, session labels and all target
threshold meanings. The QML plot spaces supplied sessions equally. CSV is
available as a host export seam. Built-in PNG/SVG file export and integrated
screen-reader verification are outside this lane; the rest of WP-031 and
integrated acceptance remain with Sol.
