<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Diagram editing

`Tk.DiagramDocument` is a reusable diagram data model. `Tk.DiagramCanvas`
binds it to a themed, keyboard-accessible node surface. The model owns stable
node, port and edge IDs, edit history and a local JSON file. The canvas owns
selection and viewport pose; it sends edits to the model by ID.

## Document API

| Operation | Effect |
| --- | --- |
| `addNode(title, x, y, stencilId)` | Add a positioned card and return its stable ID. `stencilId` is saved for a host stencil provider. |
| `addPort(nodeId, label, direction, type)` | Add a stable input or output port to a card. |
| `connectPorts(fromNode, fromPort, toNode, toPort)` | Validate endpoint direction, identity and matching port types (`any` is a wildcard), reject duplicates and return a stable edge ID. |
| `moveNode(id, x, y)`, `moveNodes(ids, dx, dy)` | Move one card or a group in one undo step. Edges keep their port associations. |
| `setNodeTitle`, `removeNode`, `disconnectEdge` | Edit labels and topology. Removing a node removes incident edges in the same step. |
| `undo`, `redo` | Restore complete graph snapshots with the same IDs. History is bounded to 128 edits. |
| `saveToFile`, `loadFromFile` | Atomically write and validate `qindatk.diagram/1` JSON. A failed load leaves the current graph intact. |

`nodes` and `edges` are read-only snapshots. Node rows contain `nodeId`,
`title`, `stencilId`, document-space `x`/`y`, and `inputs`/`outputs` with
`portId`, `label`, `type`. Edge rows contain `edgeId` and four endpoint IDs.
The native file holds these fields and a `version` of 1. A reopen keeps all
IDs; it starts a new local undo history.

## Canvas behavior

`DiagramCanvas.document` accepts a `DiagramDocument`. Drag a card to move it;
activate an output and then an input to connect them. Select a wire and press
Delete to disconnect it. Arrows navigate nodes, Space arms the selected
output, and Enter connects it to the selected input. Middle drag pans and
Ctrl+wheel zooms around the pointer. `selectedNodeId`, `selectedEdgeId`,
`zoom`, `panX` and `panY` describe the local view. The underlying `NodeGraph`
remains available through `graphView` for host-specific gestures.

`NodeGraph.wireRouting` defaults to `curved`. DiagramCanvas sets it to
`orthogonal`. Forward edges take a horizontal/vertical midpoint dogleg.
Backward edges travel through a lane above their endpoints. The route is a
pure function of endpoint positions and theme clearance, so a move produces
the same path each time and never changes edge identity. This first router
does not avoid unrelated cards or optimize crossings.

## Remaining WP-083 scope

The diagram slice has manual placement and explicit edges. Automatic graph
layout, obstacle-aware routing, multi-selection gestures, reusable visual
stencil assets, typed flow quantities and conservation checks, Sankey views,
shared domain-session history, and import/export adapters remain. The CAD
model, unit/precision policy, geometry kernel, constraint solver, 2D/3D
viewport and CAD export are separate future work. See
[WP-083](universal-toolkit/implementation/phase-07-integrations.md#wp-083).
