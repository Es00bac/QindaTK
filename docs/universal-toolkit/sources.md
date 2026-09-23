<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Sources, scope and method

Audit date: **2026-09-22**. Repository:
`/home/cabewse/work_SPaC3/QindaTK`, HEAD
`89d5ca39a13c974d2877597a39d0fe6db88fbaf0`, including local changes present during
inspection. Read the [verification record](verification.md) for the checks
actually run and their limits.

## Repository evidence

| Source | How it was used |
| --- | --- |
| [AGENTS.md](../../AGENTS.md) | Import, layout, theme, documentation, source-size and verification rules |
| [src/CMakeLists.txt](../../src/CMakeLists.txt) and [CMakeLists.txt](../../CMakeLists.txt) | Compiled/exported types, Qt minimum, linking and packaging boundaries |
| [src/qml](../../src/qml) | All 78 component files inventoried; descriptions and boundaries checked against declarations and relevant implementations |
| [src/core](../../src/core) | Named/anonymous QML registrations, stylus value type, model/layout/telemetry/theme contracts and support helpers |
| [src/bridge/QindaQtTheme.qml](../../src/bridge/QindaQtTheme.qml) | Optional desktop integration excluded from the main catalog generator |
| [controls](../controls.md), [media](../controls-media.md), [layout](../layout.md), [theming](../theming.md), [docking](../docking.md) | Existing human contracts, conventions and mismatches needing qualification |
| [architecture](../architecture.md), [decisions](../decisions.md), [verification](../verification.md) | Prior ownership choices, test conventions and known private-API boundaries |
| [tests](../../tests) | Coverage pointers and intended behaviors; tests were not executed |
| [examples](../../examples) | Demonstrations distinguished from exported reusable editors |
| [tools](../../tools) and [packaging](../../packaging) | Preview, generation, screenshot and installation infrastructure |

The source inventory was cross-checked against the current generator's output.
The generator produces 99 entries: 78 QML plus 21 C++ entries. `stylusSample` and
the QindaQt bridge raise this book's coverage to 101. The reference supplements
pointer properties and multiline invokables by reading headers; it does not
pretend the original regex JSON is complete language reflection.

At the start, tracked local changes existed in README, controls/catalog, icon
data/tooling/tests, NumberField, input tests and the gallery, plus an untracked
`.build-audit/` directory. Those are baseline context, not work credited to this
documentation task. The catalog is regenerated from the same current sources.

## Official external references

Accessed on 2026-09-22. These sources establish representative workflow needs
or available Qt primitives. The proposed APIs, priorities and ownership
recommendations are this audit's inferences. They are not vendor recommendations
or statements of QindaTK compatibility. Older guides are used for established
workflow examples, without implying they describe the latest complete release.

| Reference | Relevant example |
| --- | --- |
| [Adobe: embedded Smart Objects](https://helpx.adobe.com/photoshop/desktop/create-manage-layers/smart-objects/create-embedded-smart-objects.html) | Editable embedded content and layer organization |
| [Adobe: edit layer masks](https://helpx.adobe.com/photoshop/using/editing-layer-masks.html) | Mask data and explicit layer-versus-mask targets |
| [Adobe: thread InDesign text frames](https://helpx.adobe.com/indesign/desktop/add-and-manage-text/add-and-import-text/thread-text-frames.html) | Text flowing through linked frames |
| [Adobe: InDesign preflight](https://helpx.adobe.com/indesign/desktop/print/preflight/configure-and-use-the-preflight-panel.html) | Production issues such as fonts, links and overset content |
| [Adobe: keyframe editing](https://helpx.adobe.com/after-effects/desktop/animate-in-after-effects/animation-keyframes/setting-selecting-deleting-keyframes.html) | Timed parameter/keyframe operations |
| [Adobe: Graph Editor tutorial](https://www.adobe.com/learn/after-effects/web/adjusting-keyframes-dynamic-movement) | Editable motion curves and interpolation |
| [Microsoft: Word track changes](https://support.microsoft.com/en-us/word/training/track-changes-in-word) | Reviewable document revisions |
| [Microsoft: Excel Data Model](https://support.microsoft.com/en-us/excel/create-a-data-model-in-excel) | Related tables and pivot data |
| [Microsoft: slide masters](https://support.microsoft.com/en-us/powerpoint/training/what-is-a-slide-master-in-powerpoint) | Shared layouts and document inheritance |
| [Microsoft: presenter view](https://support.microsoft.com/en-us/powerpoint/training/use-presenter-view-in-powerpoint) | Presenter notes and separate display roles |
| [Apple: Logic Pro overview, 10.7 guide](https://support.apple.com/guide/logicpro/what-is-logic-pro-lgcpe9cc45dd/10.7/mac/11.0) | Audio/MIDI editing, mixing, automation and notation |
| [Avid: Pro Tools](https://www.avid.com/pro-tools) | Recording, comping, clip edits, mixing and sound-to-picture |
| [Avid: Media Composer editing guide, 2025.x](https://resources.avid.com/SupportFiles/attach/Media_Composer/Media_Composer_v2025.x_Editing_Guide.pdf) | Editorial timeline and multicamera workflows |
| [Qt: TableView](https://doc.qt.io/qt-6/qml-qtquick-tableview.html) | Virtualized models and editing hooks suitable for evaluation |
| [Qt: QUndoStack](https://doc.qt.io/qt-6/qundostack.html) | Reusable undo-stack primitives |
| [Qt: Accessible](https://doc.qt.io/qt-6/qml-qtquick-accessible.html) | Roles, names, states and accessible actions |

Qt's live documentation may describe APIs newer than QindaTK's Qt 6.8 minimum.
Backend evaluation must identify the exact API version required and either keep
the minimum or deliberately update it with migration/packaging tests.

## Interpretation rules

- Absence means no reusable QindaTK component/service for that behavior was found
  in this repository; Qt primitives or another application may still implement it.
- Existing source counts are inventory facts, not feature-completeness scores.
- Test filenames and comments are evidence of intent/coverage, not passing runs.
- A related display component establishes PARTIAL support, not a complete editor.
- Hypothetical names/contracts are intentionally nonbinding until implemented and reviewed.
- Formats, devices, collaboration and advanced algorithms require independent backend work.
- No accessibility, performance, portability or commercial-product parity certification is made.

To refresh the audit, regenerate the source inventory, review changed contracts,
update dates/evidence links, re-evaluate concrete findings, and rerun the
documentation checks. Preserve the distinction between current implementation,
inference, unverified risks and proposed design.
