// SPDX-License-Identifier: LGPL-3.0-or-later
#version 440
// Full-item quad for VolumeRenderNode. `pixel` carries logical item
// coordinates, so each fragment receives its pixel centre exactly as the
// reference raycaster computes it (column + 0.5, row + 0.5).

layout(location = 0) in vec2 position;
layout(location = 0) out vec2 pixel;

// AGENT-CONTRACT (volume_render_node.cpp prepare()): identical block in both
// stages; params[] is filled in the order documented in volume_raycast.frag.
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 params[8];
};

out gl_PerVertex { vec4 gl_Position; };

void main()
{
    pixel = position;
    gl_Position = mvp * vec4(position, 0.0, 1.0);
}
