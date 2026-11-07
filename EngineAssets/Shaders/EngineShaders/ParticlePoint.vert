#version 450 core

// Particle positions only (ParticlePointBuffer), drawn as GL_POINTS.
layout(location = 0) in vec3 aPos;

uniform mat4 uViewProj;
// Camera view matrix — picks the shadow cascade by view-space depth, like PBR.frag.
uniform mat4 uView;
// != 0 = this spawner's ParticleClass receives shadows (ParticleShadowMode Receive / CastAndReceive).
uniform int uReceiveShadows;
// ParticleClass::ShadowSize. The receiving point is moved this far towards the light, so a
// particle that also casts does not shadow itself.
uniform float uShadowSize;
uniform vec3 dirLightDir;

// Directional light visibility of the whole particle, 0 = fully shadowed. Computed per vertex
// rather than per fragment: a point has one depth, so every fragment would take the same tap —
// and PointSize^2 taps per particle is too much with a million of them.
flat out float vShadow;

// ==========================================================
// Directional light shadows — a copy of the ShadowData block and atlas sampler from PBR.frag
// (there is no shader include system). Must stay layout-identical to Plu::ShadowCascadeGPU and
// the rest of the block filled by Renderer::UpdateShadowDataBuffer.
// ==========================================================
#define MAX_CASCADE_COUNT 6

struct ShadowCascade
{
    mat4 viewProj;
    vec4 atlasScaleBias;
    // x = view-space end distance, y = texel size in metres, z = depth bias in [0,1] depth.
    vec4 params;
};

layout(std140, binding = 2) uniform ShadowData
{
    ShadowCascade cascades[MAX_CASCADE_COUNT];
    vec2  invAtlasSize;
    int   cascadeCount;
    float shadowFadeStart;
    float shadowFadeEnd;
    float cascadeBlendFraction;
    float normalBiasScale;
    float pcfRadiusTexels;
    int   debugVisualizeCascades;
    int   pcfTapCount;
    int   pcfRotateSamples;
    int   contactShadowSteps;
    float contactShadowLength;
    float contactShadowThickness;
    float contactShadowBias;
};

layout(binding = 15) uniform sampler2DShadow shadowCascades;

int SelectCascade(float depthView)
{
    int cascade = cascadeCount - 1;
    for (int i = 0; i < cascadeCount - 1; i++)
    {
        if (depthView < cascades[i].params.x) { return i; }
    }
    return cascade;
}

float DirectionalShadow(vec3 worldPos)
{
    if (uReceiveShadows == 0 || cascadeCount <= 0) return 1.0;

    float depthView = -(uView * vec4(worldPos, 1.0)).z;
    if (depthView >= shadowFadeEnd) return 1.0;

    int cascade = SelectCascade(depthView);

    // Half the particle's own shadow square plus a texel towards the light: past the depth the
    // particle wrote itself when it also casts.
    float texelWorld = cascades[cascade].params.y;
    vec3 offsetPos = worldPos - normalize(dirLightDir) * (uShadowSize * 0.5 + texelWorld);

    // Ortho cascade projection, w == 1.
    vec3 projCoords = (cascades[cascade].viewProj * vec4(offsetPos, 1.0)).xyz * 0.5 + 0.5;
    if (projCoords.z > 1.0) return 1.0;
    // The neighbour in the atlas is another cascade, not a border colour.
    if (any(lessThan(projCoords.xy, vec2(0.0))) || any(greaterThan(projCoords.xy, vec2(1.0))))
        return 1.0;

    vec2 atlasUv = projCoords.xy * cascades[cascade].atlasScaleBias.xy + cascades[cascade].atlasScaleBias.zw;
    float refDepth = projCoords.z - cascades[cascade].params.z;
    // Vertex stage: no derivatives, so explicit LOD 0.
    float visibility = textureLod(shadowCascades, vec3(atlasUv, refDepth), 0.0);

    float fade = clamp((depthView - shadowFadeStart) / max(shadowFadeEnd - shadowFadeStart, 1e-4), 0.0, 1.0);
    return mix(visibility, 1.0, fade);
}

void main()
{
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vShadow = DirectionalShadow(aPos);
}
