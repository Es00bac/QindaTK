<!-- SPDX-License-Identifier: LGPL-3.0-or-later -->
# Domain decisions

Continuation of [decisions.md](decisions.md) for specialized domain packages
(scientific imaging and similar); [application decisions](decisions-application.md)
and [service decisions](decisions-services.md) hold the others.

## D-086 — Voxel indices are cell centres; slices show whole cells

**Context.** The first slice view fitted its image to size × spacing but
mapped pixels between the outer voxel centres, so edge voxels drew at half
width and the in-plane aspect drifted by up to one voxel per axis (2.5% for a
40-slice, 3 mm coronal plane). Sampling, clicks and the crosshair need one
convention shared with future volume rendering.

**Decision.** A voxel index is the centre of a cell one spacing wide, as in
ITK and NIfTI. A volume samples continuous indices within −0.5 … size − 0.5;
`Nearest` picks the containing cell and `Linear` clamps to the outer centres
inside the edge half cell. A slice rectangle spans whole cells, and
`sliceImage`, `voxelAt`, `positionForVoxel` and the crosshair share that one
mapping. The cursor stays within the outer centres.

**Consequences.** Every voxel draws at its physical size and a click inside
an edge cell selects that voxel. World positions are unchanged (`world =
origin + orientation × index × spacing`). Views stay in voxel-index planes;
oblique or patient-axis reslicing needs its own resampling contract.
