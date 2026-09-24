<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# WP-076 transform controls slice

Status: implementation lane for integration; WP-076 acceptance stays with Sol.

## Delivered

- `SceneModel3D` extends its existing begin/preview/commit/cancel transaction
  with axis/plane translation in world or entity-local coordinates, scalar
  axis translation for a depth gesture, axis rotation in world/local
  coordinates, and positive uniform scale. Every preview starts from the same
  committed transform; invalid preview leaves the scene unchanged.
- `SceneView3D` presents mode, constraint and world/local controls over the
  actual Quick 3D viewport. Pointer drags call the model transaction; Escape
  and pointer cancellation retain the existing rollback path.
- The existing primitive renderer and optional posed `meshDocument` renderer
  remain the geometry sources.

## Verification

- Configured and built the isolated Qt 6.11.1 tree with the configured
  `MAKEOPTS` equivalent `-j24 -l24`; `tst_scene_model_3d` passed 1/1.
- Native xcb/OpenGL `tests/gpu/tst_scene_view_3d.qml` passed 3/3, including
  picking, drag/cancel, toolbar rotation, scale, constrained translation and
  undo. The test waits for the renderer to show the restored mesh after undo.
- Captured and opened [WP-076-gizmos.png](WP-076-gizmos.png). The toolbar,
  shaded primitives and selected cube are visible. The viewport dump at
  logical 1080x720 reported `SceneView3D#sceneView3D 12,98 1056x589`,
  `SceneView3DRenderer#sceneRenderer3D 0,0 1056x589`, and
  `Flex#sceneTransformBar3D 4,4 375x24`.

## Remaining

The toolbar is a functional transform selector, not an on-object 3D handle
gizmo. Plane drags can lose the axis parallel to the camera direction. This
slice does not provide scene orbit/pan, nonuniform scale, imported meshes,
material editing, document-session integration, Qt 6.8 qualification or a
device-loss test.
