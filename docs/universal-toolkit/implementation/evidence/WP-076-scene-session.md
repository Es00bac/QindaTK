<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-076 scene document session slice

`SceneModel3D` now implements `DocumentModel`. Each ordinary entity/material
edit and committed transform is recorded in one `UndoService`; previews are
transient and do not advance revision. A standalone model owns a private
64-step service. `DocumentSession` adopts that history when the scene is
created or opened by `DocumentManager`.

`sceneDocumentCodec()` is registered in the built-in codec registry as
`qindatk.scene3d.v1`. It fully validates the bounded scene on decode without a
QObject. It opens existing native version 1 and 2 files, and managed saves
write version 3 with document UUID, revision, reset generation and session
extensions. Direct `toJson()`/`saveToFile()` continue to write version 2.
Managed captures hold immutable bytes so editing can continue while the
session writes a save or recovery frame.

`examples/scene-session/Main.qml` exposes Save As, Reopen and shared
Undo/Redo beside a live outliner, viewport and material inspector. The
focused `tst_scene_model_3d` lifecycle case exercises shared history,
save/reopen, native version 2 import and recovery across manager destruction.

The direct CMake/Ninja build used the configured `-j24 -l24` limits.
`tst_scene_model_3d` passed, including the new save/recovery lifecycle case;
`example_scene-session` and `example_scene-3d` passed headless smoke checks.
The live QML example also instantiated under Wayland/OpenGL and produced the
[inspected screenshot](WP-076-scene-session.png). `qtk-preview --dump` measured
the outliner at 260×622, scene viewport at 548×622 and material inspector at
280×622; the Save As, Reopen, Undo and Redo actions were visible above them.
The screenshot shows the seeded cube and its shaded material sphere.

Remaining WP-076 scope includes imported mesh/resource providers, shared
material assets and texture maps, full on-object gizmos, FormatRegistry scene
export, device-loss/multi-view qualification and Qt 6.8 verification. The
scene snapshot command is intentionally bounded to 512 primitive entities;
large mesh editing needs a delta command domain.
