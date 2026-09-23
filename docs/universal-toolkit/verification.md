<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Documentation verification record

Date: **2026-09-22**. Scope: the Markdown book under `docs/universal-toolkit`,
its link from `docs/index.md`, and regeneration of `docs/catalog.json`.

This is the **original 31-document audit verification record**. The later
[complete Sol implementation plan](implementation/README.md) has its own
[planning verification record](implementation/verification.md); its additions
do not imply that the runtime checks below have now been performed.

## Completed checks

| Check | Result |
| --- | --- |
| Current component coverage | 101 unique reference entries: all 78 main-module QML files, 21 C++ catalog entries, stylusSample and QindaQtTheme |
| Current index coverage | Every reference entry indexed once; entries sorted alphabetically with category and implementation kind |
| Proposed coverage | 212 uniquely named proposals in 9 categories; all indexed by stable proposal ID |
| Proposal structure | Every named proposal has priority, purpose, contract, dependencies/boundary and completion check |
| Priority counts | P0: 13; P1: 92; P2: 91; P3: 16 |
| Local links and anchors | All relative targets and referenced Markdown/explicit anchors resolve |
| Markdown structure | Balanced code fences and consistent table column counts |
| Repository file conventions | All 31 new Markdown files have LGPL-3.0-or-later SPDX headers and are below 500 non-blank lines |
| Generated catalog | Regenerated with `python3 tools/scripts/gen_catalog.py`; parsed output equals current generator output |
| Catalog preservation | All 93 previously present entries retained without content changes; six existing media component entries added, bringing the catalog to 99 |
| Source preservation | Hash comparison against the audit snapshot found only `docs/index.md` and `docs/catalog.json` changed among pre-existing inspected files |
| Whitespace | Documentation changes checked with `git diff --check`; new Markdown checked for trailing whitespace |

The catalog's six added entries are Filmstrip, KeyCap, RangeSlider, Thumbnail,
TimeRuler and Waveform. This generator refresh does not fix the known parser
limitations described in the [source audit](reports/04-source-audit.md).

## Method

A temporary Python validation script compared current reference headings with
the source-generated inventory plus the bridge/value-type supplements. It also
checked indexes, proposal IDs, required proposal fields, relative paths,
heading/explicit anchors, table delimiters, fenced blocks, SPDX headers, file
length and source hashes. These are documentation consistency checks, not new
runtime tests or a replacement for source/API review.

The current reference supplements the existing catalog's omissions by reading
C++ pointer properties, multiline invokables and the stylus value declaration.
ObjectName expressions were inspected, including computed names and the
PillSwitcher override/fallback. Current declarations and proposed names are
kept in separately labeled reference sections.

## Not performed

No build, ctest run, qtk-preview instantiation, geometry dump, screenshot,
screen-reader test, hardware test or performance benchmark was run. The
source-level risks in the report remain marked as unverified where a runtime
reproduction is needed. No runtime implementation was changed by this task.

Existing local source/gallery/test changes were preserved. The generated catalog
retains the pre-existing PillSwitcher entry and all other prior entries. The
repository's current tests and examples are documented as evidence of coverage
intent, without claiming that they pass on this working tree.

External links cite official sources accessed during the audit; those are listed
in [sources and method](sources.md). Local documentation validation did not
perform an automated availability check of every external URL.
