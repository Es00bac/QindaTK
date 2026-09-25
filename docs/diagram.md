<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Diagram editing

`Tk.DiagramDocument` is a reusable diagram data model. `Tk.DiagramCanvas`
binds it to a themed, keyboard-accessible node surface, and
`Tk.SankeyDiagram` shows its quantities as flow bands. The model owns stable
node, port and edge IDs, edit history, routes, layout and a local JSON file.
The canvas owns selection, snapping and viewport pose; it sends edits to the
model by ID.

## Document API

| Operation | Effect |
| --- | --- |
| `addNode(title, x, y, stencilId)` | Add a positioned card and return its stable ID. `stencilId` is saved for a host stencil provider. |
| `addPort(nodeId, label, direction, type)` | Add a stable input or output port to a card. |
| `connectPorts(fromNode, fromPort, toNode, toPort)` | Validate endpoint direction, identity and matching port types (`any` is a wildcard), reject duplicates and return a stable edge ID. |
| `moveNode(id, x, y)`, `moveNodes(ids, dx, dy)` | Move one card or a group in one undo step. Edges keep their port associations. |
| `setNodeTitle`, `removeNode`, `removeNodes(ids)`, `disconnectEdge` | Edit labels and topology. Removing nodes removes incident edges in the same step; `removeNodes` is one step for a whole selection and fails without change if any ID is unknown. |
| `setEdgeFlow(edgeId, amount)` | Set a finite, nonnegative quantity (up to 1 billion) on one edge in one undo step. |
| `autoLayout(columnStep, rowStep, originX, originY)` | Explicitly arrange a directed graph by rank in one undo step. Manual positions stay untouched until called. |
| `routeEdges(metrics)` | Return `{edgeId: {points, clear}}`: orthogonal document-space polylines that avoid card interiors. |
| `sankeyLayout(width, height, nodeWidth, gap)` | Return Sankey node bars and band positions in pixels, or `{valid: false}`. Never edits the graph. |
| `flowImbalances(tolerance)` | List through nodes whose incoming and outgoing totals differ by more than `tolerance × max(1, larger total)`. |
| `exportSvg(path, metrics)`, `exportGraphMl(path)` | Atomically write an SVG drawing with the same routes, or GraphML with every stable node, port and edge ID. |
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
Within a column, components are ordered by the mean row of their
predecessors, a one-pass barycenter heuristic that removes the crossings
saved order would cause (D-123). Saved node order breaks ties. Repeated layout with
the same spacing is a no-op; one Undo restores every prior manual position.
`DiagramCanvas.autoLayout()` supplies theme-derived card spacing and origin.

`DiagramCanvas.flowMode` draws a thicker wire for larger flow and adds incoming
and outgoing totals to each card header and accessibility description. Width
uses a capped square-root scale to keep large values readable. The model
totals all incident edge quantities, including parallel and feedback edges.

## Flow conservation and Sankey bands

A node with both incoming and outgoing edges is a *through* node and should
conserve flow; nodes with only outgoing edges are sources, those with only
incoming edges are sinks. `flowImbalances(tolerance)` reports each through
node with `nodeId`, `title`, `inFlow`, `outFlow` and `imbalance`
(in − out). The model reports imbalances and never rejects them, because a
draft diagram is often unbalanced while being edited.

`sankeyLayout` reuses the layout ranking for columns and rows. A node's
value is the larger of its totals. One scale serves every column: the
smallest of (height − gaps) / column value sum, so equal flows have equal
widths everywhere. Columns are centered vertically; column *i* sits at
`i × (width − nodeWidth) / (columns − 1)`. Outgoing bands stack from the
top of the source in target order, incoming bands in source order; saved
edge order breaks ties. Each link row has `edgeId`, `fromNodeId`, `toNodeId`,
`flow`, `width`, `x0`/`y0` (source side, band center), `x1`/`y1` and
`feedback` (target not right of the source). Feedback links return through
lanes below every bar: the height reserves one gap plus the band width per
feedback link, and the scale accounts for it. A feedback band leaves the
source's right edge, turns down at `x0 + loop`, runs left along `laneY`,
rises at `x1 − loop` and enters the target from the left. Lanes nest: the
band that leaves lowest takes the innermost (highest) lane and the smallest
`loop`. Feedback bands stack last on their bars, nearest the lanes. The
outer columns reserve room for the legs, and `labelInsetRight` /
`labelInsetLeft` on each node row move that column's labels past them. Node rows have `x`, `y`,
`width`, `height`, `value`, `inFlow`, `outFlow`, `column` and `role`
(`source`, `through`, `sink` or `isolated`). An empty graph, a width not
larger than `nodeWidth`, a negative gap or a column whose gaps exceed the
height give `{valid: false}`; all-zero flows give scale 0.

`Tk.SankeyDiagram` paints that layout inside a theme margin. Bands use the
accent role; feedback bands follow their return lanes in the warning role,
so they never cross a bar. Through nodes that do
not conserve flow turn the warning role and name the imbalance in their
tooltip and accessible name. The view is read-only; a band tap sets
`selectedEdgeId` and emits `edgeSelected`. Names are `sankeyDiagram`,
`sankeyBands`, `sankeyEmpty`, `sankeyNode_<nodeId>`, `sankeyLabel_<nodeId>`
and `sankeyLink_<edgeId>`.

## Canvas behavior

`DiagramCanvas.document` accepts a `DiagramDocument`. Drag a card to move it;
activate an output and then an input to connect them. Select a wire and press
Delete to disconnect it. Ctrl+click (or Ctrl+Space on the focused card)
toggles a card in `selectedNodeIds`, the multi-selection, and focuses it;
`selectedNodeId` is the focused card, as in `NodeGraph`. Moving or nudging a
member moves every member with `moveNodes`, and Delete on a member removes
them all with `removeNodes`, each in one undo step; a card outside the set
acts alone. Selecting a wire clears the multi-selection. A positive `snapStep` rounds dropped card positions to that
document-unit grid. Ctrl+Z undoes, Ctrl+Shift+Z or Ctrl+Y redoes, and Escape
cancels a pending link or drag. With one card or wire selected, the
`diagramInspector` strip edits its title (`diagramTitleEditor`, commit with
Enter) or flow (`diagramFlowEditor`); a rejected value restores the stored
one. Arrows navigate nodes, Space arms the selected
output, and Enter connects it to the selected input. Middle drag pans and
Ctrl+wheel zooms around the pointer. `selectedNodeId`, `selectedEdgeId`,
`zoom`, `panX` and `panY` describe the local view. The underlying `NodeGraph`
remains available through `graphView` for host-specific gestures.

`NodeGraph.wireRouting` defaults to `curved`. DiagramCanvas sets it to
`orthogonal` and also supplies `NodeGraph.edgeRoutes` from
`DiagramDocument.routeEdges(routeMetrics)` (D-124). `routeMetrics` carries
the card width and height, header and row heights, the port stub and the
lane clearance the view draws with; omitted keys default to 200, the
busiest side's `24 + ports × 22 + 4`, 24, 22, 14 and 16. A forward edge
takes the midpoint dogleg when it is clear, else the vertical channel
beside a card (`stub` away) nearest the midpoint. A backward or still-blocked
edge takes a horizontal lane `stub` above or below some card, choosing the
shortest detour. A route never enters a card interior when any candidate
allows it; otherwise it takes a lane above every card and reports
`clear: false`. Routes are a pure function of the document and metrics, so a
move produces the same path each time and never changes edge identity.

## Export

`exportSvg(path, metrics)` writes an SVG in document units with the same
routes as the canvas: one `polyline` per edge (`data-edge-id`, `data-flow`),
and per card a group (`data-node-id`, `data-stencil`) with its frame, escaped
title and port markers (`data-port-id`). `DiagramCanvas.exportSvg(path)`
passes the view's metrics. `exportGraphMl(path)` writes directed GraphML
with node `title`, `stencilId`, `x`, `y`; GraphML ports named by port ID
with `direction`, `label` and `type`; and edges with `sourceport`,
`targetport` and `flow`. Both writes are atomic and fail on an empty
diagram. They are output adapters; `qindatk.diagram/1` stays the native,
reopenable format.

## Domain canvas contract

`DiagramDocument` and [`CadDocument`](cad.md) are separate models that
answer the same calls (D-122): `revision`, `canUndo`, `canRedo`,
`lastError`, `undo()`, `redo()`, `saveToFile(path)`, `loadFromFile(path)`
and `exportSvg(path)`. Their views (`DiagramCanvas`, `CadViewport`) take a
`document` and keep selection and viewport pose (`zoom`, `panX`, `panY`)
locally.

## Scope notes

Performance on large graphs, screen-reader runs and output-fidelity
comparison belong to the deferred qualification packages WP-088 to WP-094.
Visual stencil assets are provider-backed (`stencilId` is stored for the
host). A shared cross-document session history is the host's composition of
each document's own history. See
[WP-083](universal-toolkit/implementation/phase-07-integrations.md#wp-083).
