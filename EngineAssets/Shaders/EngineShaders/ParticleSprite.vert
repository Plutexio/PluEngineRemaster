#version 450 core

// Particle sprites: an attribute-less VAO drawn with glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, n).
// gl_InstanceID picks the particle from the SSBO, gl_VertexID (0..3) the quad corner.

// Must stay layout-identical to Plu::ParticleInstanceGPU (ParticleInstanceBuffer.h), 48 B.
struct ParticleInstance
{
    vec4 positionRotation; // xyz world position, w rotation (radians)
    vec4 sizeAndVelocity;  // xy half extents, zw view-space motion (m/s)
    uint colorRGBA8;
    uint subUVFrameFlags;  // low 16 bits frame
    uint pad0;
    uint pad1;
};

layout(std430, binding = 7) readonly buffer ParticleInstances
{
    ParticleInstance particles[];
};

uniform mat4 uView;
uniform mat4 uProjection;
// 0 = camera facing (rotated by positionRotation.w), 1 = stretched along the view-space motion.
uniform int uFacingMode;
// Velocity stretch: extra length per m/s of on-screen speed.
uniform float uStretchFactor;
// Atlas layout. Frames run left to right, top to bottom.
uniform int uSubUVColumns;
uniform int uSubUVRows;

out vec2 vUV;
// Quad corner in [-1, 1], for the untextured soft disc.
out vec2 vLocal;
out vec4 vColor;

void main()
{
    ParticleInstance p = particles[gl_InstanceID];

    vec2 corner = vec2(float(gl_VertexID & 1), float(gl_VertexID >> 1)) * 2.0 - 1.0;
    vec3 centre = (uView * vec4(p.positionRotation.xyz, 1.0)).xyz;
    vec2 halfSize = p.sizeAndVelocity.xy;

    // The quad lives in view space, so it faces the camera by construction.
    vec2 axisX;
    vec2 axisY;
    vec2 motion = p.sizeAndVelocity.zw;
    float speed = length(motion);
    if (uFacingMode == 1 && speed > 1e-4)
    {
        // Texture up = direction of motion. The extra length trails behind the particle, so the head
        // stays where the particle is.
        axisY = motion / speed;
        axisX = vec2(axisY.y, -axisY.x);
        float stretch = 0.5 * speed * uStretchFactor;
        halfSize.y += stretch;
        centre.xy -= axisY * stretch;
    }
    else
    {
        float c = cos(p.positionRotation.w);
        float s = sin(p.positionRotation.w);
        axisX = vec2(c, s);
        axisY = vec2(-s, c);
    }

    vec2 offset = axisX * (corner.x * halfSize.x) + axisY * (corner.y * halfSize.y);
    gl_Position = uProjection * vec4(centre + vec3(offset, 0.0), 1.0);

    // Sub-UV cell. Textures are flipped on load (v = 1 is the top of the image), so row 0 sits at the top.
    ivec2 grid = max(ivec2(uSubUVColumns, uSubUVRows), ivec2(1));
    int frame = int(p.subUVFrameFlags & 0xFFFFu) % (grid.x * grid.y);
    vec2 cell = vec2(float(frame % grid.x), float(frame / grid.x));
    vec2 local = corner * 0.5 + 0.5;
    vUV = vec2((cell.x + local.x) / float(grid.x), 1.0 - (cell.y + 1.0 - local.y) / float(grid.y));

    vLocal = corner;
    vColor = unpackUnorm4x8(p.colorRGBA8);
}
