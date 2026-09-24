<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# 3D scene model and viewport

`SceneModel3D` is a small persistent scene hierarchy. `SceneView3D` renders it
through Qt Quick 3D and supports picking, constrained translation, rotation,
uniform scale, snapping, keyboard movement and cancellation. This is a reusable toolkit slice, with
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

## Manipulation and history

`beginTransform(id)` starts one transaction. Each
`previewWorldTranslation(delta, snap)` is a world-space offset **from the initial
position**, not from the last preview. Positive snap rounds world coordinates to
that grid; zero disables snapping. The model converts the result through the
parent's inverse transform. `commitTransform()` creates one history step;
`cancelTransform()` restores the exact initial scene. Structural and numeric
edits are rejected during a gesture. `undo()` and `redo()` hold up to 64 snapshots.
`canUndo`, `canRedo`, `editing` and `lastError` expose state to the host.

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
Graphics device initialization failures are reported by Qt. Qt owns render
resource lifetime.

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
world point to viewport pixels/depth; `pickEntity(x,y)` returns the nearest visible
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
`sceneMode3D_<mode>`, `sceneAxis3D_<constraint>`, `sceneSpace3D`, and
`sceneEntity3D_<stable UUID>`. The viewport has an accessible graphic role/name
and supports keyboard selection movement. A future outliner must supply complete
keyboard traversal of entities.

## Persistence and verification

`toJson()`/`fromJson()` use version 1 `QindaTK.Scene3D`, retaining IDs, hierarchy,
primitives, quaternion transforms and appearance. Camera/selection stay per-view.
Saving during a preview writes the committed state. `saveToFile(path)` uses
atomic replacement; `loadFromFile(path)` rejects files larger than 4 MiB.
Failed parsing leaves the current scene/history intact. Successful loading is
undoable. This native scene format is not a general mesh interchange format.

Run `QT_QPA_PLATFORM=xcb qtk-preview examples/scene-3d/Main.qml --gpu --grab scene.png` to see the
actual renderer. `tst_scene_model_3d` covers rotated/scaled parent reparenting,
world snapping, cancel/undo, saving during a preview, reload and inherited
visibility in one focused scenario.

`Tk.SceneOutliner { scene: sceneModel }` shows the entity hierarchy with
stable IDs, keyboard selection/traversal, expand/collapse, per-entity visibility,
and drag-to-parent reparent requests. `keepWorldOnDrop` explicitly chooses
whether a drop preserves the world transform or keeps local coordinates. The
outliner does not rename entities because the scene model has no rename command.

## Remaining WP-076 scope

On-object 3D gizmo handles, constrained scale and per-axis scale;
instance and imported mesh/resource providers; material property editing and
asynchronous material preview; application document
session/shared undo integration; device loss and multiple-view qualification;
Qt 6.8 validation and accessibility traversal. This slice does not claim WP-076
acceptance, renderer-independent materials, nonuniform transforms or CAD accuracy.

The optional GPU interaction check runs separately from the toolkit's software
suite: `QT_QPA_PLATFORM=xcb QT_QUICK_BACKEND=rhi QSG_RHI_BACKEND=opengl
qmltestrunner -import build/wp076/qml -input tests/gpu/tst_scene_view_3d.qml`.
It exercises actual mesh picking, world-to-pixel mapping, drag/undo and Escape
within a still-held pointer press. An OpenGL-capable display/platform is required.
