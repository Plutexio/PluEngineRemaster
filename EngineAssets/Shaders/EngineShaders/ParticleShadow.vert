#version 450 core

// Shadow caster pass of the particles (Renderer::DrawParticleShadowCasters): the same positions as
// ParticlePoint.vert, drawn as GL_POINTS into a directional cascade or a spot shadow slot.
layout(location = 0) in vec3 aPos;

// Light's projection * view for the cascade / spot slot being drawn.
uniform mat4 uViewProj;
// Side of one particle's shadow square, in metres (ParticleClass::ShadowSize).
uniform float uShadowSize;
// Shadow-map texels per metre at w == 1: projection[0][0] * 0.5 * resolution. Dividing by w makes
// the one formula right for the ortho cascades (w is always 1) and the perspective spot slots.
uniform float uSizeScale;

void main()
{
    gl_Position = uViewProj * vec4(aPos, 1.0);
    gl_PointSize = max(uShadowSize * uSizeScale / gl_Position.w, 1.0);
}
