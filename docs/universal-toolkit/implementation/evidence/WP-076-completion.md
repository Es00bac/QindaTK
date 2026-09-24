<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-076 completion slice: gizmo, render lifecycle and multiple views

Lane branch `work/wp076-finish`, based on integration head `3313138`. This
slice adds only what the numbered WP-076 steps and "Accept when" still needed
after the [core](WP-076.md), [transform controls](WP-076-gizmos.md),
[material](WP-076-materials.md) and [session](WP-076-scene-session.md) slices.

## Added

- `Tk.TransformGizmo3D` (`src/qml/TransformGizmo3D.qml`). It draws on-object
  handles for the selected entity in world or local orientation: X/Y/Z arrows
  and XY/XZ/YZ plane squares, one ring per axis, and a uniform-scale circle.
  The handles stay `armLength` screen pixels long at any camera distance. Each
  drag is one `SceneModel3D` transaction (axis, plane, rotation or scale
  preview from the press, commit on release). Escape, a lost grab, hiding the
  gizmo, a scene change or a renderer reset cancel it. Presses that miss a
  handle, and the inner 30% of the arm, fall through to the viewport's existing
  pick, drag and camera gestures. Accessible Button targets call `nudge()`.
  The projection adapter is `projectWorldPosition` plus `viewRay`, so any view
  can host the gizmo. `SceneView3D` embeds one.
- `SceneView3D`: `viewRay(x,y)` (`meshRay` now delegates to it),
  `renderGeneration`, `resetRenderer()`. When Qt invalidates the window's
  scene graph (device loss, or a non-persistent hide), the view cancels its
  gestures and replaces its Quick 3D renderer. The replacement rebuilds every
  GPU resource from the scene and mesh models. Views in other windows are
  untouched.
- Documentation in `docs/scene-3d.md`: identity and units (scene, entity,
  camera and material identity), transform gizmo, render resources and device
  loss, multiple views, and scope limits. Also updated `docs/controls.md`, the
  regenerated `docs/catalog.json` (450 types), the scene example captions and
  the gallery 3D section, which now selects its cube so the gizmo shows.

## Requirement disposition

| WP-076 step | Evidence |
| --- | --- |
| 1. IDs, hierarchy, units, picking, resource lifecycle | Scene `documentId`/`revision`, entity UUIDs, material = owning entity ID, camera = per-view state (docs/scene-3d.md "Identity and units"). Hierarchy/picking from earlier slices. The lifecycle is documented and tested below. |
| 2. Functional renderer, persistent model, fallback reporting | Earlier slices; unchanged (`rendererAvailable`, unavailable state on software). |
| 3. Outliner selection, world/local gizmos, material bindings | `TransformGizmo3D` world/local handles, with the headless and GPU tests below. The outliner and material slices are unchanged. |
| 4. Camera controls, viewport sizing, device loss, save/reopen | Resize and scene-graph loss are covered by `tst_scene_view_3d_render`. Camera and session features come from the earlier slices. |
| 5. Verify hierarchy, picking, material output, multi-view resources, cancel/undo, reload | Two windows share one scene and mesh; only the lost window's renderer is replaced. Gizmo cancel and undo are tested. The other items are covered by earlier slices. |
| Accept when | View (Quick 3D), select (pick, outliner), transform (gizmo, toolbar drags, keys), shade (materials) and persist (native save/reopen, `DocumentSession`) all run through reusable `Tk` surfaces. |

Proposal completion checks: VID-19 picking and manipulation across a resize
and a scene-graph loss are covered by `tst_scene_view_3d_render`. Serialized
transforms are camera-independent because cameras are never saved. VID-21: a
gizmo drag and the numeric edits give the same transform for a child of a
rotated, scaled parent, and the handles keep their screen length at 2× camera
distance. VID-20 and VID-22 are as in the earlier slices, apart from the scope
notes below.

## Results (Qt 6.11.1, Gentoo, this host)

Build: `build/wp076` (Debug, Ninja, ccache), `cmake --build --parallel 4`
for `qindatkplugin qtk-preview tst_qml tst_scene_model_3d
tst_scene_view_3d_render`, as the lane instructions specify.

- Headless (`offscreen`/`software`): `tst_qml -input` passed
  `tst_transform_gizmo_3d.qml` **7/7**, `tst_material_preview.qml` **6/6** and
  `tst_scene_outliner.qml` **5/5**. The focused ctest set passed **7/7**:
  `tst_scene_model_3d`, `tst_scene_view_3d_render` (skipped headless by
  design), `example_scene-3d`, `example_scene-session`, `example_gallery`,
  `example_mesh-rig-3d` and `example_mesh-rig-live`.
- GPU (`xcb`, `QT_QUICK_BACKEND=rhi`, `QSG_RHI_BACKEND=opengl`):
  - `qmltestrunner -input tests/gpu/tst_scene_view_3d.qml` passed **5/5**,
    including the existing navigation and pick/drag/cancel cases with the
    gizmo present. The new gizmo case drags an arrow at 2× camera distance:
    the object tracks the pointer within 1 px and ignores off-axis motion. It
    also checks a +90° ring turn, Escape mid-drag and undo.
  - `tst_scene_view_3d_render` passed **3/3** on the threaded render loop.
    Both windows first pick the cube and the mesh. A 400×300 resize keeps
    projection and picking. A committed edit reaches both windows. Window A is
    then hidden mid-drag and its scene graph is invalidated. The gesture is
    cancelled, the committed transform is restored with history intact, and
    A's renderer is destroyed and recreated (generation incremented). Window B
    keeps its renderer, generation and picking. Showing A again gives a new
    initialization with drawn pixels (object ≠ background), a cube and mesh
    pick, and a new undoable drag. Closing B leaves A working.
- qmllint (module response options) on the changed module files:
  `TransformGizmo3D.qml` has no warnings. `SceneView3D.qml` has the same 13
  warnings as at the base revision (Quick 3D members on `Loader.item`,
  unqualified delegate access), none of them new.
- `git diff --check` passed. Non-blank lines: gizmo 410, SceneView3D 210.

Captures (logical 1080×720 at 1.5× scale, `--gpu --wait 3000`), both opened and
inspected. Each shows the arrows and plane squares on the selected parent cube;
comfortable density gives longer arms and larger targets:
[compact](WP-076-gizmo-compact.png), [comfortable](WP-076-gizmo-comfortable.png).

```text
compact:     SceneView3D#sceneView3D 274,0 498x589
             TransformGizmo3D#transformGizmo3D 0,0 498x589
             Item#transformGizmoHandle3D_x 236,292 14x14
comfortable: SceneView3D#sceneView3D 275,0 436x572
             Item#transformGizmoHandle3D_x 220,281 17x17
```

## Scope notes

- **Driver-level device loss and other backends.** Real GPU resets
  (robustness or TDR) on each platform, Vulkan/Metal/Direct3D, and
  mixed-scale displays are platform qualification: WP-088 step 2 ("render
  context loss on each claimed configuration"). Here the real Qt invalidation
  and re-initialization path ran on xcb/OpenGL.
- **Qt 6.8, installed consumer, full ctest.** These are integrator checkpoint
  gates, per the lane instructions. The code uses no API newer than Qt 6.4.
- **Imported meshes, instancing, lock state (VID-20 contract).** None of the
  numbered steps requires them. The scene is real Quick 3D primitives plus
  posed `MeshRigDocument` geometry. Instancing and locking would need new
  model state and are not claimed.
- **Material assets, texture maps, procedural shaders, render-quality tiers
  (VID-22 contract).** Materials stay one per entity. The preview is a
  synchronous scene-graph binding to the latest committed snapshot on one
  reused sphere and material. There is no queued render request to cancel,
  and a rapid edit allocates no new GPU resources.
- **Nonuniform or per-axis scale.** The model contract is uniform positive
  scale, so the gizmo offers uniform scale only.
- **Numeric transform inspector control, FormatRegistry scene export.** Not
  required. Gizmo and numeric agreement is proven against the model's numeric
  API, and native save/reopen is the persistence requirement.
- **Decision record.** The overlay-gizmo and renderer-reset choices are local
  to the 3D package and documented in `docs/scene-3d.md`, so no `D-` entry
  was added.

## Acceptance

All numbered steps and "Accept when" now have evidence, and the omitted
proposal features are recorded above. **Ready for acceptance**, subject to the
integrator's checkpoint gates on the merged tree (full ctest, Qt 6.8,
installed consumer).
