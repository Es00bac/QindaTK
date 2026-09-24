<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Mesh, rig, UV and texture document

`Tk.MeshRigDocument` is a bounded, host-owned triangle mesh editing model. It
contains actual rest geometry, bones, weights, UV corners, seams and sRGB texture
pixels. It evaluates posed positions and exports an OBJ mesh plus PNG texture.
This output can be imported by an application renderer. `SceneModel3D` currently
renders Qt Quick 3D primitives only, so `SceneView3D` does not display this mesh.

## Geometry and rig

`addVertex(position)` returns a stable vertex ID. `addFace(v0,v1,v2)` returns a
stable face ID and creates three stable UV corner IDs. Triangles with repeated
vertices, zero rest area, duplicate topology or edges shared by more than two
faces are rejected. `setVertexPosition` preserves incident face validity;
`removeFace` and `removeVertex` permit explicit topology cleanup. A vertex
with incident faces cannot be removed. `vertices` contains `vertexId`, rest
`position`, evaluated `posedPosition`, and `weights`; `faces` contains `faceId`,
ordered `vertexIds`, `uvIds`, `uv` points and per-edge `seams`. Posed positions
do not change rest geometry or identities.

`addBone(name,parentId,head,tail)` creates an ordered, acyclic hierarchy. The
head and tail are in mesh rest coordinates. An existing parent must precede the
new bone. `setBoneLimits(id,minimum,maximum)` defines inclusive Euler degree
limits within −180 to +180 for each axis; a change that invalidates the current
pose or excludes the zero reset pose is rejected. `setBonePose(id,eulerDegrees)` evaluates a rotation about the
rest head, composed through its parent's deformation transform. The `bones`
snapshot exposes these fields. `setVertexWeights(vertexId,map)` accepts at most
four positive bone weights summing to one; an empty map leaves a vertex fixed
at its rest position. Weighted vertices use deterministic linear blend skinning.
`resetPose()` restores all zero rotations without baking vertex positions.

## UV and paint

Each face corner has its own UV ID and a coordinate in normalized `[0,1]`
texture space, with `v=0` at the bottom. `setUV(faceId,corner,point)` changes a
corner without changing mesh topology. `setSeam(v0,v1,bool)` marks an existing
mesh edge. `uvIslands` groups faces connected across unmarked edges;
`translateUvIsland(faceId,delta)` moves all UV corners in that group and rejects
coordinates that would leave `[0,1]`. These operations keep all vertex, face
and UV corner IDs stable. This is explicit UV editing, not automatic unwrap or
packing. A seam annotation does not alter UV coordinates by itself.

`resizeTexture(width,height)` creates an opaque white premultiplied RGBA
texture, up to 1024 pixels per side. Painting is a transaction:
`beginPaint()` → `paintRay(origin,direction,color,radiusPixels,opacity)` samples
→ `endPaint()`. The ray selects the nearest forward hit on the **posed** mesh;
barycentric face-corner UVs map the hit into the texture. Samples paint actual
pixels with source-over composition. An individual sample can cross a seam by
issuing rays on each side; no stroke interpolation is claimed. A miss reports
`lastError` and leaves the stroke open. `cancelPaint()` restores the initial
pixels. The model allows at most 8,192 samples and 16 million estimated stamp
pixels per stroke; exceeding the bound cancels it. The texture is a single
sRGB channel. Perspective camera ray creation and brush spacing are the host's
responsibility; a future `TexturePaintView` will supply them.

## Undo, persistence and output

Each geometry, rig or UV command is one undo step. A complete paint stroke is
one step. The last 16 states are retained; redo clears on a new edit. `toJson()`
and `saveToFile(path)` use version 1 `qindatk.mesh-rig` JSON, including all stable
IDs, rest geometry, rig, UVs, seams and PNG texture bytes. Saving during a paint
stroke records the committed pre-stroke state. `fromJson()` and `loadFromFile()`
validate into temporary state, leaving the current document intact on failure.
Document input and output are limited to 16 MiB. Reopen preserves the posed
output and texture, with a fresh document object history.

`exportObj(path)` atomically writes posed vertices, per-corner UV coordinates
and triangle faces; `exportTexturePng(path)` atomically writes the same pixels
shown by `textureDataUrl`. OBJ does not carry bones, weights, seam marks or
animation; the native JSON does. The OBJ/PNG pair is an actual reusable output
path while the current scene viewport lacks arbitrary mesh rendering.

Limits: 4,096 vertices, 8,192 triangles, 128 bones, four weights per vertex,
1,024² texture pixels, 16 history states. This slice omits automatic unwrap,
UV packing, arbitrary topology repair, inverse kinematics, procedural skin
weighting, material channels, texture tiles and GPU mesh display.
