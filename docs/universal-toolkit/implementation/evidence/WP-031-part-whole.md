<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-031 part-to-whole lane evidence

**State:** focused PartWhole lane complete; WP-031 remains active. Other
analytical and scheduling families require their own integration gates.

## Data and geometry contracts

- `PartWholeModel` accepts at most 4096 ordered, uniquely identified nodes
  with parent depth at most 8. Replacement is atomic. Finite nonnegative
  values are known parts; zero is known zero, null/absent is missing, and
  signed negative input is rejected. Parent totals may be declared only
  when all children are known and their sum agrees within 1e-9 relative
  tolerance. An incomplete parent exposes its known subtotal separately.
- Pie/donut and sunburst use exact known-total angle fractions; the donut
  hole has radius 0.5 of the plot radius. Treemap alternates partitions,
  with each leaf area proportional to its known share. Zero and missing
  remain in the model and accessible table with distinct statuses, but have
  no area or angle. Geometry and hit testing return stable IDs.
- `PartWholeView` provides themed pointer and keyboard selection, a virtual
  `DataTable` alternative, accessible total/selection summary, and bounded
  1–8192 pixel PNG/SVG export through the on-screen drawing routine.

## Focused verification

The isolated `build/wp031-partwhole` directory was configured from commit
`1f14cbb`. The system's `MAKEOPTS` was read as `-j24 -l24` and left
unchanged; builds used plain `cmake --build`.

- The full build completed. `tst_part_whole` and `example_part-whole` passed
  under CTest; the focused QML file passed 3/3 cases.
- Known-answer fixtures checked 1:3 pie angles of 90°/270°, treemap areas
  of 0.25/0.75, the half-radius donut hole, and a nested sunburst band.
  Invalid parent sums, missing descendants with declared totals, negative
  values, depth and node caps, and retained selection IDs were exercised.
- Software renderer output checked red/green pixels for the known treemap
  regions. The exported PNG and rasterized SVG were checked at those same
  coordinates; output bounds were checked.
- The example instantiated and dumped at compact dark and comfortable light
  densities. `PartWholeView#partWholeView` measured `12,12 876x556` in
  compact and `14,14 872x552` in comfortable. Both grabs were opened and
  inspected for distinct nested sectors, readable labels and total footer:
  [compact](../../../screenshots/part-whole-compact.png),
  [comfortable](../../../screenshots/part-whole-comfortable.png).

The family has no signed contribution layout: negative input is rejected by
the declared part-to-whole contract. Missing magnitudes cannot contribute to
an exact denominator, so angles and areas explicitly use the known subtotal.
