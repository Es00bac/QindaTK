// SPDX-License-Identifier: LGPL-3.0-or-later
#version 440
// AGENT-CONTRACT (docs/volume-imaging.md#volume-rendering, D-146): this is
// VolumeRaycaster::shade() (src/core/volume_raycast.cpp) on the GPU. Keep the
// ray origin, slab clip, sample positions, transfer lookup, opacity
// correction, termination and maximum-intensity rule identical to it.

layout(location = 0) in vec2 pixel;
layout(location = 0) out vec4 fragColor;

// params[0] = centre.xyz, item opacity
// params[1] = right.xyz (index per pixel), view width
// params[2] = down.xyz (index per pixel), view height
// params[3] = direction.xyz (index per world unit), step (world units)
// params[4] = box low.xyz, opacity correction exponent
// params[5] = box high.xyz, mode (0 composite, 1 maximum intensity)
// params[6] = 1 / size.xyz, value scale of the normalized texture
// params[7] = domain min, 1 / domain span, termination opacity, maximum steps
layout(std140, binding = 0) uniform buf {
    mat4 mvp;
    vec4 params[8];
};
layout(binding = 1) uniform sampler3D volumeTexture;
layout(binding = 2) uniform sampler2D transferTexture;

vec4 transfer(float value)
{
    float t = clamp((value - params[7].x) * params[7].y, 0.0, 1.0);
    // Entry k is centred at texel k: linear filtering between entries.
    return texture(transferTexture, vec2((t * 255.0 + 0.5) / 256.0, 0.5));
}

float sampleAt(vec3 index)
{
    // Voxel index i is the centre of texel i (D-086); ClampToEdge clamps
    // the edge half cell like the reference Linear sample.
    return texture(volumeTexture, (index + 0.5) * params[6].xyz).r * params[6].w;
}

void main()
{
    vec3 origin = params[0].xyz + (pixel.x - 0.5 * params[1].w) * params[1].xyz
                + (pixel.y - 0.5 * params[2].w) * params[2].xyz;
    vec3 direction = params[3].xyz;
    float tNear = -3.0e38;
    float tFar = 3.0e38;
    for (int axis = 0; axis < 3; ++axis) {
        if (abs(direction[axis]) < 1.0e-12) {
            if (origin[axis] < params[4][axis] || origin[axis] > params[5][axis]) {
                fragColor = vec4(0.0);
                return;
            }
        } else {
            float t1 = (params[4][axis] - origin[axis]) / direction[axis];
            float t2 = (params[5][axis] - origin[axis]) / direction[axis];
            tNear = max(tNear, min(t1, t2));
            tFar = min(tFar, max(t1, t2));
        }
    }
    if (tFar <= tNear) {
        fragColor = vec4(0.0);
        return;
    }
    bool maximum = params[5].w > 0.5;
    int steps = int(params[7].w);
    vec3 colour = vec3(0.0);
    float alpha = 0.0;
    float best = 0.0;
    bool found = false;
    for (int k = 0; k < steps; ++k) {
        float t = tNear + (float(k) + 0.5) * params[3].w;
        if (t >= tFar)
            break;
        float value = sampleAt(origin + t * direction);
        if (isnan(value))
            continue;
        if (maximum) {
            if (!found || value > best) {
                best = value;
                found = true;
            }
            continue;
        }
        vec4 entry = transfer(value);
        if (entry.a <= 0.0)
            continue;
        float a = 1.0 - pow(1.0 - entry.a, params[4].w);
        float weight = (1.0 - alpha) * a;
        colour += weight * entry.rgb;
        alpha += weight;
        if (alpha >= params[7].z)
            break;
    }
    if (maximum && found) {
        vec4 entry = transfer(best);
        colour = entry.rgb * entry.a;
        alpha = entry.a;
    }
    fragColor = vec4(colour, alpha) * params[0].w;
}
