<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Typed image graph

`NodeGraphModel` is a reusable, bounded image-processing graph behind the
`NodeGraph` editor. It owns stable node/edge/group IDs, typed ports, graph
positions, parameters, groups, undo/redo and native topology persistence.
Source pixels remain host-owned: after loading a graph, call
`setSourceImage(nodeId, taggedImage)` for every source before rendering. This
lets an application choose its own decoder, asset store and project format.

## Nodes, ports and parameters

One type table (`src/core/node_graph_types.cpp`) defines every node type, its
typed ports and its parameter identity. `nodeTypes` publishes it for hosts:
`{type, title, inputs, outputs, parameters}`, where each parameter is
`{parameterId, label, valueType (number|color|string|choice), defaultValue}`
plus `minimum`/`maximum`, `maximumLength` or `options`. Unknown keys, wrong
value types and out-of-range values are rejected; omitted parameters use the
default.

| Type | Inputs | Output | Parameters | Evaluation |
| --- | --- | --- | --- | --- |
| `source` | — | `image` | `sourceId` string (≤128) | Draw the host's tagged sRGB image at the canvas origin. |
| `solid` | — | `image` | `color` CSS color (default transparent) | Fill the output canvas with a solid color. |
| `mask` | — | `mask` | `shape` (`rectangle`, `ellipse`); `x`, `y`, `width`, `height` pixels (default 0, 0, 100, 100) | Opaque antialiased shape on a transparent canvas; alpha is the mask. |
| `transform` | `image` | `image` | `x`, `y` translation; `scale` (0.001–1000); `rotation` degrees (±3600) | Scale and rotate about the canvas centre, then translate, with smooth sampling. |
| `blend` | `background`, `foreground` (`image`); optional `mask` (`mask`) | `image` | `opacity` (0–1); `mode` (`normal`, `multiply`, `screen`, `overlay`, `darken`, `lighten`, `difference`, `add`) | Cut the foreground by the mask's alpha when connected, then composite it over the background with the mode and opacity. |
| `output` | `image` | — | — | Publish the connected pixels. |

Port snapshots are `{portId, label, type, optional}`. `connectPorts` rejects
unknown nodes/ports ("Unknown node or port"), mismatched port types
("Incompatible ports: mask output cannot connect to image input"), self links
and cycles ("Connection would create a cycle"), occupied inputs and the edge
limit before editing. `nodes`, `edges` and `groups` are snapshot lists with the
stable keys consumed by `NodeGraph`: `nodeId`, `type`, `title`, `x`, `y`,
`parameters`, `inputs`, `outputs`; `edgeId`, `fromNodeId`, `fromPortId`,
`toNodeId`, `toPortId`; and `groupId`, `title`, `nodeIds`. The view emits
requests; the host invokes `addNode`, `removeNode`, `moveNode`,
`setParameters`/`setParameter`, `connectPorts`, `disconnectEdge`,
`groupNodes(nodeIds,title)`, `ungroup`, `moveGroup(groupId,dx,dy)`, and sends
the resulting snapshots back to the view. A rejected edit leaves the graph and
revision unchanged and sets `lastError`.

Removing a node removes its edges and its group membership; an emptied group
is removed. A later request that names the removed node or its ports is
rejected like any unknown port.

## Edits, transactions and history

Every successful edit is one local undo step (128 kept). A gesture that makes
many edits wraps them: `beginEdit()` captures the graph, edits in between
coalesce, `commitEdit()` leaves one undo step (none if nothing changed), and
`cancelEdit()` restores the captured graph and the redo stack. While an edit is
open, `editing` is true, `canUndo`/`canRedo` are false and `undo`/`redo` are
rejected; a second `beginEdit` is rejected. Loading a file closes any open
edit.

## Evaluation

`render(outputNodeId,width,height)` evaluates an output node and returns
`{ok, imageDataUrl, width, height, revision, evaluatedNodeIds}` or
`{ok:false, error}`. Changed source pixels, parameters or connections
invalidate only the affected node and its downstream cache; moves, groups and
renders of an unchanged graph evaluate nothing (`evaluatedNodeIds` lists the
cache misses of that render). Changing canvas size clears the cache. Output is
deterministic for the same graph, sources and canvas. Evaluation errors
(unconnected required input, missing source pixels, missing output) keep the
last good preview and are recoverable by editing, rebinding or undo.
`setSourceImage` requires an explicit sRGB tag. Processing is on encoded sRGB
pixels through Qt's QPainter; it does not claim linear-light compositing or
calibrated display output. Evaluation is synchronous and bounded by the
limits below; long-running render jobs and their cancellation belong to a
render queue.

## Storage and limits

`saveToFile` atomically writes `qindatk.node-graph` version 2 JSON (version 1
plus `groups`); `loadFromFile` accepts versions 1 and 2, rejects newer or
malformed files, and validates a detached graph (types, ports, parameters,
duplicate inputs, cycles, group membership) before replacing the current one.
The file stores topology, parameters, groups, source IDs and revision, but not
source pixel bytes. Loading clears the local undo stack and needs the host to
rebind source images. The implementation supports at most 1,000 nodes, 2,000
edges, 4 MiB of JSON, 16 MP per input/output image, 256 MiB of source pixels,
128 MiB of cached intermediate images, and 128 local history steps. The host
is responsible for project-level asset recovery, other node types and
device/GPU effects.

Removing a source node or changing its `sourceId` releases its external pixel
binding. Undo or `cancelEdit` restores graph records; the host must bind
pixels again before rendering.
