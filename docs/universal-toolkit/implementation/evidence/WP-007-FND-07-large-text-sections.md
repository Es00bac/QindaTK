<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-007 FND-07 — large text in gallery sections

## Reproduction

The `InputsSection`, `SelectionSection`, and `PopupsSection` examples were
rendered at 480×320 with Touch density, `Tk.Density.textScale = 3`, and both
high-contrast presets. Each uses a `Tk.Scroll` host to keep its long content
reachable.

The first captures exposed two related example-layout defects. The section
note had an unbounded intrinsic width, so it consumed all of the header's
horizontal flex space and pushed the actual section title to zero width. The
fixed 110px label column also elided longer row labels such as `CheckBox` at
3× text.

## Repair

`Tk.SectionHeader` now moves an oversized note under the title and wraps it to
the available width. Its implicit height includes the wrapped note, so the
section body starts below it. Compact notes remain inline. `GalleryRow` moves a
label whose measured text width exceeds its normal 110px column above the
controls, using the existing Flex wrap layout. The dark and light high
contrast themes use the same Theme text and surface roles as before.

`SectionHeader` retains `sectionTitle` and names wrapped notes
`sectionCount_<title>`. Gallery sections expose `gallerySection_<title>` and
their row labels expose `galleryRowLabel` for preview and regression checks.

## Verification

`tests/qml/tst_touch_targets.qml` extends the 480×320 Touch 3× gallery test to
filter to Selection, check that the section title retains nonzero width, the
wrapped note fits inside the header, and the full `CheckBox` label has room.
The focused `tst_qml` suite passed on Qt 6.11.1 and Qt 6.8.3. `all_qmllint`
also exits successfully on both; warnings are existing module diagnostics.

Each scope was captured with `qtk-preview --dump` and `--grab`, using both
`high-contrast-dark` and `high-contrast-light`. The six opened software
captures are in the ignored
`build/wp007-large-text/wp007-{inputs,selection,popups}-high-contrast-{dark,light}.png`.
For example, the Inputs dump reports:

```text
SectionHeader  0,0 430x199
  Overline#sectionTitle  0,6 427x37  "INPUTS"
  Caption#sectionCount_Inputs  0,51 430x148  "TextField · SearchField · TextArea · NumberFi..."
  GalleryRow  0,0 430x259
    Caption#galleryRowLabel  0,0 430x37  "TextField"
    Flex  0,49 430x210
      TextField  0,0 180x48
```

The pre-fix dump had `Overline#sectionTitle 0x37`, an overwide count at
1140×37, and the first body row beginning at y=60 beneath the overflowing
text. After repair the section header is 199px tall and its first row begins
after that header; long row labels use the full available width on their own
line.

## Limits

This is an example-gallery qualification at maximum supported text scaling in
a small host. It does not qualify every application-specific layout or native
assistive technology behavior.
