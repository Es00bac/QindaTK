<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# 3D scene model and viewport

`SceneModel3D` is a small persistent scene hierarchy. `SceneView3D` renders it
through Qt Quick 3D and supports picking, on-object transform handles
(`TransformGizmo3D`), constrained translation, rotation, uniform scale,
snapping, keyboard movement and cancellation. This is a reusable toolkit slice, with
512 primitive entities maximum; it does not provide a mesh authoring app.

## Model

Create `Tk.SceneModel3D`, then call `addEntity(name, primitive, position, color)`.
Supported primitives are `cube`, `sphere`, and `cylinder`, using Qt Quick 3D's
built-in meshes (nominal size 100 world units). The return value is a stable UUID.
`entities` is a read-only snapshot list with `entityId`, `parentId`, `name`,
`primitive`, local `position`, `eulerDegrees`, `scale`, resolved `worldPosition`,
`worldRotation`, `worldScale`, `color`, inherited `visible`, and `localVisible`
fields. `localVisible` reports the entity's own visibility setting; `visible`
remains the effective value after parent visibility is inherited.

**Identity and units.** A scene is identified by `documentId` (a UUID) and its
committed `revision`; entities by stable UUIDs that survive save and reopen.
Materials have no separate ID: each entity owns exactly one material, addressed
by the entity ID, and its revision is the scene revision that committed it.
Cameras are view state, not scene data: each `SceneView3D` owns one camera
(`cameraPosition`, `cameraEulerDegrees`, `cameraTarget`), identified by that
view, never saved and never in undo, so serialized transforms do not depend on
any view. Lengths are world units (primitives are 100 units across), angles
are degrees and scale is a uniform factor.

Transforms use right-handed coordinates, degrees and positive **uniform** scale.
`setTransform(id, position, eulerDegrees, scale)` edits local coordinates.
`worldTransform(id)` returns position, quaternion rotation and uniform scale.
`reparent(id, parentId, keepWorld)` explicitly preserves either the world transform
or the existing local transform. An empty parent ID selects the root. Cycles and
hierarchies deeper than 32 links are rejected without changing the scene.
Uniform scale ensures exact composition and inversion under rotated parents;
nonuniform scale and shear are outside this contract. Local positions are limited
to one million units; local scales to 0.001–1000 and resolved scales to 1e-6–1e6.

`setAppearance(id, color, visible)` edits the base color and visibility. A hidden
parent hides descendants. `removeEntity(id)` requires children to be reparented
first. `select(id)` changes selection; the empty ID clears it. Selection is view
state and does not add an undo record.

Each primitive has one editable material: `setMaterial(id, baseColor,
roughness, metalness, emissiveColor)` updates its Qt Quick 3D shaded output.
Roughness and metalness must be finite values in `[0,1]`; colors must be valid.
`material(id)` returns the committed properties. Material edits use the scene's
64-step undo history, and `entities` projects their values so all open views
refresh. `Tk.MaterialPreview { scene: sceneModel }` follows the selected entity,
shows an actual shaded sphere on a GPU backend, and exposes base color,
roughness, metalness and emission fields. `entityId` can select an entity
independently. The preview displays an unavailable message on software Qt.
This slice uses one material per entity; shared material assets, maps and
procedural nodes are still outside the model.

## Manipulation and history

`beginTransform(id)` starts one transaction. Each
`previewWorldTranslation(delta, snap)` is a world-space offset **from the initial
position**, not from the last preview. Positive snap rounds world coordinates to
that grid; zero disables snapping. The model converts the result through the
parent's inverse transform. `commitTransform()` creates one history step;
`cancelTransform()` restores the exact initial scene. Structural and numeric
edits are rejected during a gesture. `undo()` and `redo()` hold up to 64 snapshots.
`canUndo`, `canRedo`, `editing` and `lastError` expose state to the host.
Standalone scenes use a private `UndoService`; when `DocumentManager` creates
or opens a `qindatk.scene3d.v1` session, the same history moves to the shared
service. The session handles dirty state, asynchronous save, recovery, reopen
and workspace document identity. All committed entity/material edits and
transform gestures enter that one history. Previews do not change the
document revision or trigger autosave.

`previewConstrainedTranslation(delta, constraint, space, snap)` projects a
world-space drag delta onto `x`, `y`, `z`, `xy`, `xz`, or `yz` axes; `free` retains
the original camera-plane/world-grid behavior. `space` is `world` or `local`
using the selected entity's orientation at gesture start. Constrained snap
rounds each allowed displacement, so an unsnapped starting position stays on
the chosen axis. `previewAxisTranslation(distance, axis, space, snap)` supplies
a scalar drag distance for an axis viewed head-on. `previewRotation(degrees, axis, space, snapDegrees)` rotates
around `x`, `y`, or `z` in world or local space. `previewUniformScale(factor,
snap)` scales uniformly, with optional absolute world-scale increments. Every
preview replaces the previous preview from the same `beginTransform` state.
Cancel restores the original and commit records one undo step.

## View and renderer

`Tk.SceneView3D { scene: sceneModel }` loads `SceneView3DRenderer` on an RHI backend.
The separate renderer imports Qt Quick 3D. CMake includes it only when the
optional Qt6Quick3D package is present; the runtime module must also be installed.
SDKs without Quick3D still build the model and control, with an unavailable state. Other QindaTK surfaces and the C++ scene model do not require it.
The Qt Quick software adaptation displays an explicit unavailable message.
`rendererAvailable` reports a suitable Qt graphics backend and successfully
loaded renderer component; module loading failure displays an explicit message.
Graphics device initialization failures are reported by Qt.

**Render resources and device loss.** Each view loads its own
`SceneView3DRenderer`; the Qt Quick 3D models, materials, geometry and textures
it uploads belong to that renderer and its window's scene graph.
`renderGeneration` counts renderer instances. When Qt invalidates the window's
scene graph (a graphics device loss, or hiding a window whose graphics and scene
graph are not persistent), the view cancels its gestures and replaces the
renderer; the replacement rebuilds every resource from `SceneModel3D` and
`MeshRigDocument` when the scene graph initializes again. `resetRenderer()`
performs the same replacement on request. Scene data, selection, history and
cameras are unaffected, and views in other windows keep their renderers.

**Multiple views.** Any number of views may show one model. Each owns its
camera, renderer and GPU resources. The model owns entities, selection and the
single active transaction, so a committed edit reaches every view and only one
gesture runs at a time. A resized view resizes its projection and picking.

Set `meshDocument` to a `Tk.MeshRigDocument` to upload its current posed
triangle snapshot, UV coordinates and sRGB texture alongside primitive scene
entities. The optional `Tk.MeshRigGeometry` adapter updates on document edits.
The model still owns mesh data, history and OBJ/PNG output; the GPU resources
belong to the view. `meshRay(x,y)` returns a world-space origin and direction
for a host paint gesture, and `meshPicked(worldPosition)` reports a mesh hit.
The mesh objectName is `sceneMeshRig3D`. This adapter is available only when
the Qt Quick 3D module is built and the view has a GPU backend.

The view exposes `cameraPosition`, `cameraEulerDegrees`, `cameraTarget`, `translationSnap`,
`rotationSnap`, `scaleSnap`, `transformMode` (`translate`, `rotate`, `scale`),
`transformConstraint` (`free`, axes or planes), `transformSpace` (`world`,
`local`) and
`entityPicked(entityId, worldPosition)`. `projectWorldPosition(position)` maps a
world point to viewport pixels/depth; `viewRay(x,y)` returns the world-space
`origin` and `direction` through a pixel; `pickEntity(x,y)` returns the nearest visible
mesh hit (`entityId`, `worldPosition`, `distance`) or an empty object. Coordinates
are relative to the view content, with no padding. A left press uses Qt's actual nearest
mesh hit test, selects that stable ID and starts a camera-plane drag. Pixel to
world mapping uses the picked depth, so zoom does not change the contract.
The toolbar selects mode, axis or plane and world/local coordinates. Rotation
uses an axis (`z` when the constraint is `free`); scale is uniform. Horizontal
motion controls rotation and diagonal motion controls scale. A world Z-axis
translation viewed head-on uses vertical pointer motion for depth. Release
commits; Escape and pointer cancellation restore the initial state.
Plane drags project camera-plane motion onto the chosen axes; at a camera angle
parallel to one plane axis, the hidden axis needs camera navigation or the
single-axis depth gesture.
Replacing the scene or destroying the renderer cancels its active gesture.
Arrow keys translate the selection along world X/Y by one unit or the snap step.
Hosts may bind camera properties to their own navigation controls.
Right drag orbits around `cameraTarget`, middle drag pans camera and target
together, and the wheel zooms along the sight line. The same behavior is exposed
as `orbitCamera(dx, dy)`, `panCamera(dx, dy)` and `zoomCamera(wheelSteps)` so hosts
can use other devices. Camera gestures are view state and do not enter scene undo.

Internal objectNames: `sceneView3D`, `sceneRenderer3D`, `sceneTransformBar3D`,
`sceneMode3D_<mode>`, `sceneAxis3D_<constraint>`, `sceneSpace3D`,
`sceneEntity3D_<stable UUID>` and the gizmo's names below. The viewport has an
accessible graphic role/name and supports keyboard selection movement;
`SceneOutliner` supplies keyboard traversal of entities.

<a id="transform-gizmo"></a>
## Transform gizmo

`Tk.TransformGizmo3D` draws on-object handles for the selected entity; every
`SceneView3D` contains one that follows its `transformMode`, `transformSpace`
and snaps. Translate mode shows X/Y/Z arrows and XY/XZ/YZ plane squares,
rotate mode one ring per axis, scale mode a circle for uniform scale. In
`local` space the handles follow the entity's world orientation, otherwise the
world axes. Handles keep `armLength` screen pixels (theme `size.action` × 2) at
any camera distance and are draggable within `hitTolerance` (half of
`size.handle`). The inner 30% of the arm belongs to the object, so the viewport
still picks and drags it there; any press that misses a handle falls through.

One drag is one `SceneModel3D` transaction: `beginTransform` on press, previews
measured from the press, `commitTransform` on release (one undo step). An arrow
previews `previewAxisTranslation` with the pointer's travel along the drawn
arrow in world units at the pivot; a plane square previews
`previewConstrainedTranslation` with the pointer ray's delta on that plane; a
ring previews `previewRotation` with the angle swept around the projected
pivot, signed by whether the axis faces the camera; the circle previews
`previewUniformScale` with the ratio of pointer distances from the pivot.
Escape, pointer cancellation, hiding the gizmo, a scene change or a renderer
reset cancel. The model performs the parent conversion, so a gizmo drag and the
same numeric `previewAxisTranslation` or `setTransform` edit give the same
transform for parented entities. Arrows within about 11° of the sight line,
nearly edge-on plane squares and rings within about 6° of edge-on are neither
drawn nor draggable; orbit the camera or use the toolbar constraint.

`view` is the projection adapter: `projectWorldPosition(vector3d)` returns
`vector3d(x, y, depth)` in the gizmo's pixels (negative depth hides it) and
`viewRay(x, y)` returns world-space `{origin, direction}`; the host calls
`refresh()` when its projection changes. `SceneView3D` is the standard
adapter. Read-only `handles` exposes the drawn geometry (`origin`, `length`,
`axes`, `planes`, `rings`); `activeHandle` and `hoveredHandle` name handles
(`x`, `y`, `z`, `xy`, `xz`, `yz`, `uniform`). `handleAt(x, y)` hit-tests and
`nudge(handle, steps)` commits one step (translation snap or 1 unit, rotation
snap or 15°, scale × 1.25). Accessible Button targets
`transformGizmoHandle3D_<handle>` expose press/increase/decrease actions that
call `nudge`. ObjectNames: `transformGizmo3D`, `transformGizmoCanvas3D`,
`transformGizmoPointer3D`.

## Persistence and verification

`toJson()`/`fromJson()` use version 2 `QindaTK.Scene3D`, retaining IDs, hierarchy,
primitives, quaternion transforms and material values. Version 1 scenes still
load with default roughness 0.6, metalness 0 and black emission. Camera/selection stay per-view.
Saving during a preview writes the committed state. `saveToFile(path)` uses
atomic replacement; `loadFromFile(path)` rejects files larger than 4 MiB.
Failed parsing leaves the current scene/history intact. Successful loading is
undoable. This native scene format is not a general mesh interchange format.
The shared `sceneDocumentCodec()` reads these existing files by content,
assigning a stable identity derived from legacy file bytes, then writes version
3 with document UUID, revision, reset generation and session extensions.
Direct `saveToFile` remains version 2 for existing consumers; a managed
`DocumentSession.requestSaveAs` writes version 3. The live
`examples/scene-session/Main.qml` shows save, reopen and shared undo.

Run `QT_QPA_PLATFORM=xcb qtk-preview examples/scene-3d/Main.qml --gpu --grab scene.png` to see the
actual renderer. `tst_scene_model_3d` covers rotated/scaled parent reparenting,
world snapping, cancel/undo, saving during a preview, reload and inherited
visibility in one focused scenario. `tst_transform_gizmo_3d.qml` drives the
gizmo headlessly through an exact orthographic adapter: screen-constant handles,
a local-axis drag under a rotated, scaled parent that matches the numeric
edits, plane, ring and scale drags, cancellation, fall-through and accessible
nudges.

`Tk.SceneOutliner { scene: sceneModel }` shows the entity hierarchy with
stable IDs, keyboard selection/traversal, expand/collapse, per-entity visibility,
and drag-to-parent reparent requests. `keepWorldOnDrop` explicitly chooses
whether a drop preserves the world transform or keeps local coordinates. The
outliner does not rename entities because the scene model has no rename command.

## Scope limits

Not provided: imported mesh files, instancing and lock state, shared material
assets, texture maps and procedural shaders, asynchronous or quality-tiered
material rendering, nonuniform and per-axis scale, scene export through format
providers, and application-level session UX. Device loss is handled through
Qt's scene-graph invalidation; driver-level resets on each platform and
mixed-scale displays are platform qualification. No renderer-independent
material model or CAD accuracy is claimed.

The GPU checks run separately from the toolkit's software suite and need an
OpenGL-capable display:

```sh
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl \
  qmltestrunner -import build/wp076/qml -input tests/gpu/tst_scene_view_3d.qml
QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl \
  build/wp076/tests/tst_scene_view_3d_render
```

The first exercises actual mesh picking, world-to-pixel mapping, drag/undo,
Escape within a still-held press and the gizmo's arrow, ring and cancel
gestures at two camera distances. The second resizes a view, keeps two windows
on one scene and one posed mesh document, hides one non-persistent window mid-drag on the threaded render
loop, checks that only that renderer is replaced, then shows it again and
verifies drawn pixels, picking and a new undoable drag. Under ctest's software
backend it is skipped.
