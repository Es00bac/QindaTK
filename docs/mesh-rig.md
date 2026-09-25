<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Mesh, rig, UV and texture document

`Tk.MeshRigDocument` is a bounded, host-owned triangle mesh editing model. It
contains actual rest geometry, bones, constraints, animation keys, weights, UV
corners, seams and three texture channels. It evaluates posed positions at a
frame and exports an OBJ mesh plus PNG channels. When Qt Quick 3D is
available, `SceneView3D.meshDocument` also displays the posed triangles and
current channels through the toolkit's optional GPU adapter.

## Geometry and weights

`addVertex(position)` returns a stable vertex ID. `addFace(v0,v1,v2)` returns a
stable face ID and creates three stable UV corner IDs. Triangles with repeated
vertices, zero rest area, duplicate topology or edges shared by more than two
faces are rejected. `setVertexPosition` preserves incident face validity;
`removeFace` and `removeVertex` permit explicit topology cleanup. A vertex
with incident faces cannot be removed. `vertices` contains `vertexId`, rest
`position`, evaluated `posedPosition`, and `weights`; `faces` contains `faceId`,
ordered `vertexIds`, `uvIds`, `uv` points and per-edge `seams`. Posed positions
do not change rest geometry or identities. `setVertexWeights(vertexId,map)`
accepts at most four positive bone weights summing to one; an empty map leaves
a vertex fixed at its rest position. Weighted vertices use deterministic linear
blend skinning.

## Rig, constraints and animation

`addBone(name,parentId,head,tail)` creates an ordered, acyclic hierarchy. The
head and tail are in mesh rest coordinates. An existing parent must precede the
new bone. `setBoneLimits(id,minimum,maximum)` defines inclusive Euler degree
limits within −180 to +180 for each axis; a change that excludes the static
pose, any animation key, or the zero reset pose is rejected rather than
clamping stored data. A bone rotates about its rest head, composed through its
parent's deformation transform.

Each bone's rotation is evaluated at `currentFrame` (D-155):

1. **Source pose.** With no keys, the static `pose`. With keys
   (`setPoseKey(boneId,frame,euler)`, frames 0–100,000, at most 1,024 per bone),
   linear interpolation between neighbouring keys, holding the first and last.
2. **Constraints.** `addConstraint(boneId,"copyRotation",targetId,influence)`
   adds `influence × target's evaluated rotation` per Euler axis (influence 0–1,
   at most four per bone). A constraint that would make a bone depend on itself
   through any chain is refused with a "dependency cycle" error, both when
   added and when a file is loaded.
3. **Limits.** The sum is clamped to the bone's limits.

`bones` reports `pose`, `sourcePose` (step 1 at the current frame),
`evaluatedPose` (after steps 2 and 3), `posedHead`/`posedTail`, `animated`,
`animationBound`, `constraints` and `keys`.
`evaluatedPose(boneId)` returns step 3 directly. Evaluation never writes into
poses or keys. `setBonePose` on an animated bone keys the current frame
(auto-key); on a static bone it sets `pose`. `resetPose()` clears static poses
and keeps keys. Inverse kinematics and procedural skin weighting are outside
this model.

### Rig authoring

The rig is edited by stable bone ID (RigEditor's **Rig** mode,
`RigAuthoringPanel`): `renameBone(id,name)`, `setBoneRest(id,head,tail)`,
`setBoneParent(id,parentId)` and `removeBone(id)`, plus `addBone` and
`setBoneLimits`. Reparenting refuses a descendant as parent and reorders bones
so parents precede children. `removeBone` refuses a bone that anything still
refers to — children, vertex weights, a constraint target, private keys or
bound timeline lanes — so an authoring edit never rewrites or drops animation
data silently; the bone's own constraints go with it. Each edit is one undo
step.

### Timeline animation (WP-073)

`animationModel` takes the toolkit's keyframe engine, an `AutomationModel`
(D-291). `bindBoneAnimation(boneId)` adds three angle lanes
`meshrig/bone:<boneId>/rotation.x|y|z` (degrees, −180…180, linear) and copies
the bone's private keys into them at `frame × ticksPerFrame`; the private keys
stay in the mesh file, so the document still evaluates the same animation
without the model. A bound bone's source pose is read from its lanes at
`timeTicks`, so `KeyframeTimeline`, `DopeSheet` and `CurveEditor` over the same
`AutomationModel` show and edit rig keys with exact ticks and every curve
interpolation. `setBonePose`, `setPoseKey` and `removePoseKey` on a bound bone
key the lanes (one automation history step per call); `setBoneLimits` refuses
limits that exclude a lane value. The host saves the `AutomationModel` next to
the mesh, as `MotionComposition` does.

One playhead drives both: setting `currentFrame` sets `timeTicks = frame ×
ticksPerFrame`, and setting `timeTicks` (exact decimal ticks, the
`AutomationModel` timebase) sets `currentFrame` to the containing frame. The
playhead is evaluation input, not an undo step.

### Edit gestures

`beginEditGesture()` … `endEditGesture()` bracket a continuous gesture such as
a slider drag. Every commit inside shares one merge key, so the gesture is one
undo step and cannot evict older steps from the bounded history; a bound
bone's pose previews during the gesture and is keyed once at its end.
`RigEditor` emits `editGestureStarted`/`editGestureFinished` around its pose
and weight slider drags for the host to forward.

## UV topology

Each face corner has its own UV ID and a coordinate in normalized `[0,1]`
texture space, with `v=0` at the bottom. `setUV(faceId,corner,point)` changes a
corner without changing mesh topology. `setSeam(v0,v1,bool)` cuts or clears an
existing mesh edge. `uvIslands` groups faces connected across unmarked edges
and reports each island's `bounds`. All operations below keep every vertex,
face and UV corner ID and refuse results that leave `[0,1]`:

- `translateUvIsland(faceId,delta)` and
  `transformUvIsland(faceId,translation,rotationDegrees,scale)` move the island
  about its bounds centre.
- `stitchSeam(v0,v1)` welds a seam edge shared by two faces: the second face's
  island receives the rotation, uniform scale and translation that land its
  seam edge on the first face's edge, so its shape is kept and both corners of
  each seam vertex coincide; the seam mark is removed. Faces already in one
  island stitch only when their corners already coincide.
- `unwrapUvIsland(faceId)` projects an island onto the plane of its
  area-weighted normal, scaled uniformly to fill `[0,1]` on its longer side. An
  island that folds over that plane (a face turning away from it, or a closed
  surface) is refused; cut seams first.
- `packUvLayout(margin)` previews a shelf packing (tallest island first, one
  uniform scale for every island, `margin` gaps, 0–0.25) as island `bounds`;
  `packUvIslands(margin)` applies exactly that layout.

Automatic seam placement and conformal unwrapping are outside this model.

## Texture channels and paint

The document holds three channels of the same size (D-156): `baseColor` (sRGB
colour, default white), `roughness` and `metalness` (linear scalars stored as
gray, defaults 0.8 and 0). `resizeTexture(width,height)` resets all three, up
to 1024 pixels per side. `texture(channel)`, `channelDataUrl(channel)` and
`textureDataUrls` expose them; `textureDataUrl` is the base colour.

Painting is a transaction: `beginPaint(channel)` →
`paintRay(origin,direction,color,radiusPixels,opacity,hardness)` samples →
`endPaint()`. Scalar channels paint the colour's gray value. A ray selects the
nearest forward hit on the **posed** mesh. The dab is a sphere on the surface
whose radius is `radiusPixels` texels measured on the hit face (D-154): every
face within reach paints the texels whose centre maps back inside it, so one
dab continues across a UV seam into the other island. Coverage and
source-over compositing are the same kernel `TiledRasterDocument` uses
(`raster_brush_p.h`), so a hardness and opacity mean the same thing on a
canvas and on a mesh. Consecutive samples within a stroke are joined by dabs
cast along interpolated camera rays at 0.2 × radius spacing, so a stroke
follows the surface. A miss reports `lastError` and leaves the stroke open.
`cancelPaint()` restores the initial pixels. A stroke allows at most 8,192
dabs and 16 million visited texels. Each dab visits only its footprint on each
face within reach — the box around the disk where the sphere cuts the face
plane, mapped into texels — so its cost is about (2 × radius + 2)² texels per
face regardless of texture size. A sample past either bound is refused with
"Paint stroke work limit reached"; the stroke and every pixel it already
painted stay, and `endPaint()` commits them.

`paintUv(uv,color,radiusPixels,opacity,hardness)` paints in texture space
(D-292): the dab is centred on the surface point whose UV is `uv` (the first
face in document order where islands overlap), so `TexturePaintView` paints
exactly the texel under its pointer, and the dab still crosses seams. Samples
are joined at the same spacing in texels. `texel(channel,x,y)` reads one
unpremultiplied pixel.
`pickFace(origin,direction)` returns the `faceId`, `uv`, `position` and
`distance` of the hit without painting.

### Camera rays

`cameraRay(position,eulerDegrees,fieldOfView,viewportSize,point)` and the
static `perspectiveRay` compute a ray on the CPU using Qt Quick 3D's default
`PerspectiveCamera` convention (looks down −Z, `QQuaternion::fromEulerAngles`,
vertical field of view). `SceneView3D.meshRay(x,y)` uses it with the view's
`cameraPosition`, `cameraEulerDegrees` and `cameraFieldOfView`, which also
drive the GPU camera. Painting therefore produces identical texels with any
graphics backend, with no renderer at all, and after device loss; headless
tests check those texels exactly. The static `projectPoint` is its exact
inverse, and `cameraOverlay(...)` projects a face and the posed bones.

### Painting and selecting in the 3D view

`SceneView3D.meshTool` chooses what a left click on the mesh does (D-292):
`"paint"` emits `meshStrokeBegan`, one `meshPaintRayRequested(origin,
direction)` per pointer sample through the pixel under the pointer, then
`meshStrokeFinished` (Escape emits `meshStrokeCancelled`); the host routes them
to `beginPaint`/`paintRay`/`endPaint` with `TexturePaintView`'s brush and
channel. `"select"` emits `meshBonePicked(boneId)` near a posed bone and
otherwise `meshFacePicked(faceId, uv)`. `selectedMeshFaceId` and
`selectedBoneId` draw the host's selection over the mesh, so UV-editor and
rig-editor selections show in 3D. The layer (`sceneMeshLayer3D`) is CPU-only:
it works with no RHI renderer, and right/middle drags still orbit and pan.

## GPU upload

`MeshRigGeometry` packs the posed triangles (position, face normal, UV with v
flipped for top-first rows) and uploads each channel as raw straight-alpha
RGBA8 rows. `SceneView3DRenderer` maps base colour, roughness (red) and
metalness (red) onto a `PrincipledMaterial` whose factors are 1, so the
uploaded bytes alone decide the material. The static `packVertices` and
`packTexture` produce exactly the uploaded bytes; `tst_mesh_rig_geometry`
compares them with the document state. On-screen qualification under real
backends belongs to WP-088 to WP-094.

## Undo, persistence and output

Each geometry, rig, UV, key, constraint, resize or load command is one step in
an `UndoService` history, a complete paint stroke is one step, and an edit
gesture is one step (D-157). A standalone document keeps a private service (16
steps); `undoService` exposes it, and `UndoService.adoptModel(document)` moves
the history into a shared service (one service shared with `SceneModel3D` and
canvas models). Steps are before/after snapshots whose unchanged texture
channels share storage.

<a id="sessions"></a>
### Sessions

The registered `DocumentCodec` `qindatk.mesh-rig.v2` (kind `meshrig`, suffix
`.qtkmesh`) puts the document in a `DocumentSession`:
`DocumentManager.createDocument("qindatk.mesh-rig.v2")` or `openDocument(path)`
gives mesh, rig and texture edits the toolkit's save, autosave, crash recovery
and dirty tracking, like `SceneModel3D`. A session file is the version 2 JSON
plus a `session` object (`documentId`, `revision`, `resetGeneration`,
`extensions`); plain `saveToFile` files (version 1 or 2) open through the same
codec with an identity derived from their bytes. An open paint stroke never
enters a saved or autosaved file.

`toJson()` and `saveToFile(path)` write version 2 `qindatk.mesh-rig` JSON with
all stable IDs, rest geometry, rig, constraints, keys, UVs, seams and the three
PNG channels. Version 1 files (one texture, no constraints or keys) still
open, with default roughness and metalness; the base colour texture is still
required, and a file without it is refused rather than loaded with empty
channels that could not be saved and reopened. Saving during a paint stroke
records the committed pre-stroke state. `fromJson()` and `loadFromFile()`
validate into temporary state, including constraint cycles, key order and key
limits, leaving the current document intact on failure; a successful load is
one undo step. Document input and output are limited to 16 MiB.

`exportObj(path)` atomically writes positions evaluated at `currentFrame`,
per-corner UV coordinates and triangle faces; `exportTexturePng(path,channel)`
atomically writes one channel's pixels. OBJ does not carry bones, weights,
constraints, keys or seam marks; the native JSON does.

Limits: 4,096 vertices, 8,192 triangles, 128 bones, four weights per vertex,
four constraints and 1,024 keys per bone, 1,024² texture pixels per channel.
Texture tiles (UDIM), normal/emissive channels and arbitrary topology repair
are outside this model.
