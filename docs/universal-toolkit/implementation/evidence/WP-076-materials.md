<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-076 material editing and preview slice

`SceneModel3D` now owns per-entity base color, roughness, metalness and emission.
`setMaterial` validates them, records one ordinary scene undo step and updates
the `entities` projection consumed by `SceneView3DRenderer`. The actual
Qt Quick 3D `PrincipledMaterial` now reads all four values. Native scene JSON
version 2 persists them; version 1 files continue to load with defaults.

`Tk.MaterialPreview` follows the selected entity or accepts an explicit ID.
Its inspector edits the authoritative scene model. An optional Quick3D
renderer displays a shaded sphere from the same material snapshot, with a
clear unavailable state on the software backend. The scene example places
the inspector beside the actual viewport and outliner.

Verification on Qt 6.11.1: `tst_scene_model_3d` and the scene example smoke
test passed 2/2. The focused Wayland/OpenGL QML run of
`tst_material_preview.qml` passed 6/6, including a scene fixture created
before parenting and the visible hex fields after startup. The example's
`qtk-preview --dump` showed a 272×452 `materialPreviewSurface`, a 218×24 base
color field containing `#35bbc4`, and a 226×24 emission field containing
`#080d10`. [Inspected shaded GPU screenshot](WP-076-material-gpu.png) shows
the scene cube and its material preview sphere, with edited roughness 0.22
and metalness 0.75. The renderer sometimes captured an empty frame at short
wait times; a 4.5-second capture produced the inspected image.

This slice remains per-entity primitive material editing. Shared material
assets, texture maps, procedural shaders, imported meshes, shared document
undo/session integration, device-loss handling, Qt 6.8 qualification and
multi-view validation remain in WP-076.

Cherry-picked as `8970afe`. The combined merged build and focused CTest 6/6
passed; the MaterialPreview QML suite passed 6/6 in integration. The shaded
GPU capture above was opened and inspected.
