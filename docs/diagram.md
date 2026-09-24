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
| `setEdgeFlow(edgeId, amount)` | Set a finite, nonnegative quantity (up to 1 billion) on one edge in one undo step. |
| `autoLayout(columnStep, rowStep, originX, originY)` | Explicitly arrange a directed graph by rank in one undo step. Manual positions stay untouched until called. |
| `undo`, `redo` | Restore complete graph snapshots with the same IDs. History is bounded to 128 edits. |
| `saveToFile`, `loadFromFile` | Atomically write and validate `qindatk.diagram/1` JSON. A failed load leaves the current graph intact. |

`nodes` and `edges` are read-only snapshots. Node rows contain `nodeId`,
`title`, `stencilId`, document-space `x`/`y`, and `inputs`/`outputs` with
`portId`, `label`, `type`, plus derived `inFlow` and `outFlow` totals. Edge rows
contain `edgeId`, four endpoint IDs and `flow` (default 1).
The native file holds these fields and a `version` of 1. A reopen keeps all
IDs; it starts a new local undo history.
Older version-1 files without `flow` reopen with value 1 on each edge.

Layout collapses strongly connected components first, then puts each component
in a left-to-right rank. Nodes in a cycle share a column. Parallel links count
once for rank calculation, while each remains a separate persistent edge.
Within a column the saved node order is the tie breaker. Repeated layout with
the same spacing is a no-op; one Undo restores every prior manual position.
`DiagramCanvas.autoLayout()` supplies theme-derived card spacing and origin.

`DiagramCanvas.flowMode` draws a thicker wire for larger flow and adds incoming
and outgoing totals to each card header and accessibility description. Width
uses a capped square-root scale to keep large values readable. The model
totals all incident edge quantities, including parallel and feedback edges;
it does not enforce flow conservation.

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

The diagram slice has manual and automatic placement and explicit quantified
edges. Obstacle-aware routing, multi-selection gestures, reusable visual
stencil assets, conservation checks, true Sankey band placement,
shared domain-session history, and import/export adapters remain. The separate
[CAD sketch model and viewport](cad.md) provide a first 2D slice; advanced
geometry, 3D modeling and CAD export remain. See
[WP-083](universal-toolkit/implementation/phase-07-integrations.md#wp-083).
