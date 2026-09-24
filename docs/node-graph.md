<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Typed image graph

`NodeGraphModel` is a reusable, bounded image-processing graph behind the
`NodeGraph` editor. It owns stable node/edge IDs, typed input ports, graph
positions, parameters, undo/redo and native topology persistence. Source
pixels remain host-owned: after loading a graph, call `setSourceImage(nodeId,
taggedImage)` for every source before rendering. This lets an application
choose its own decoder, asset store and project format.

## Nodes and ports

| Type | Inputs | Output | Parameters | Evaluation |
| --- | --- | --- | --- | --- |
| `source` | — | `image` | optional `sourceId` string | Draw the host's tagged sRGB image at the canvas origin. |
| `transform` | `image` | `image` | `x`, `y` pixel translation; `scale` (0.001–1000) | Draw a transformed input onto a transparent canvas. |
| `blend` | `background`, `foreground` | `image` | `opacity` (0–1) | Source-over the foreground over the background. |
| `output` | `image` | — | — | Publish the connected pixels. |

Every port has type `image`. The model rejects missing nodes/ports, occupied
input ports, self links and cycles before editing. `nodes` and `edges` are
snapshot lists with the stable keys consumed by `NodeGraph`: `nodeId`,
`title`, `x`, `y`, `inputs`, `outputs`; and `edgeId`, `fromNodeId`,
`fromPortId`, `toNodeId`, `toPortId`. The view emits requests; the host invokes
`connectPorts`, `disconnectEdge`, `moveNode`, and sends the resulting snapshots
back to the view. A rejected edit leaves the graph and revision unchanged.

`render(outputNodeId,width,height)` evaluates an output node and returns a
tagged PNG data URL for preview. Changed source pixels, parameters or
connections invalidate only the affected node and its downstream cache;
changing canvas size clears the cache. A failed render keeps the last good
preview. `setSourceImage` requires an explicit sRGB tag. Processing is on
encoded sRGB pixels through Qt's QPainter; it does not claim linear-light
compositing or calibrated display output.

## Storage and limits

`saveToFile` atomically writes `qindatk.node-graph` version 1 JSON;
`loadFromFile` validates a detached graph before replacing the current one.
The file stores topology, parameters, source IDs and revision, but not source
pixel bytes. Loading clears the local undo stack and needs the host to rebind
source images. The first implementation supports at most 1,000 nodes, 2,000
edges, 4 MiB of JSON, 16 MP per input/output image, 256 MiB of source pixels,
128 MiB of cached intermediate images, and 128 local history steps. The host
is responsible for project-level asset recovery, other node
types and device/GPU effects.

Removing a source node or changing its `sourceId` releases its external pixel
binding. Undo restores graph records; the host must bind pixels again before
rendering.
