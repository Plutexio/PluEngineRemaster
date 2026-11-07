//
// Created by Plutex on 6/9/26.
//

#ifndef PLUENGINE_RENDERUTILS_H
#define PLUENGINE_RENDERUTILS_H
#include <cstddef>

#include <glm/gtc/constants.hpp>

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"

namespace Plu
{
    constexpr float kCameraNearClip = 0.1f;
    constexpr float kCameraFarClip  = 100000.0f;
    // Shadow range is no longer a global constant — it is a per-light setting
    // (DirectionalLight::ShadowDistance, default 150 m) carried in the render snapshot.

    // Maximum number of directional cascades. Sizes the GLSL array inside the ShadowData uniform
    // block, so it is a hard upper bound — the runtime count (DirectionalLight::ShadowCascadeCount)
    // is clamped to it. Raising it costs 96 B of uniform block per slot and NOTHING per frame:
    // the number of depth passes is the runtime count, not this.
    //
    // Six is where the falloff runs out of road — at ShadowResolutionFalloff = 2 the sixth cascade
    // is already at 1/4 of the base resolution, and halving further buys nothing a cascade that
    // distant can show.
    constexpr Int32 kMaxShadowCascades = 6;

    // Floor for the per-cascade resolution falloff. Below this a cascade covering tens of metres
    // stops resolving buildings, not just detail, and the atlas saves almost nothing anyway.
    constexpr Int32 kMinCascadeResolution = 256;

    // Dane pojedynczej kaskady cienia dla światła kierunkowego (CSM)
    struct ShadowCascadeData
    {
        Matrix4 ViewProj;       // lightProj * lightView dla tej kaskady
        // The two factors of ViewProj, kept separately because the depth pass sets them as the
        // shader's own "view" and "projection" uniforms rather than a premultiplied matrix — that
        // is what lets one vertex shader (the material's own) serve the lighting pass and the
        // depth passes with an identical gl_Position expression. ViewProj stays for everything
        // that needs the product itself (frustum extraction, the shadow data buffer).
        Matrix4 View;
        Matrix4 Proj;
        float   SplitDistance;  // odległość (w przestrzeni widoku kamery) końca tej kaskady
        // World-space size of one shadow-map texel in this cascade (2*Radius / Resolution).
        // Drives the receiver-side normal offset — a bias expressed in texels is the only one
        // that stays correct across cascades of wildly different world extents.
        float   TexelWorldSize = 0.0f;
        // Radius of the bounding sphere of this cascade's camera sub-frustum, in metres.
        // Also the half-extent of the ortho projection in x/y.
        float   Radius = 0.0f;
        // Depth range (zFar - zNear) of this cascade's ortho projection, in metres. Converts a
        // world-space depth bias into the cascade's [0,1] depth: bias01 = biasMetres / DepthRange.
        float   DepthRange = 0.0f;
        // Side of this cascade's square region in the shadow atlas, in texels. Cascades no longer
        // share one resolution (see ComputeCascadeResolutions), so it is carried per cascade.
        Int32   Resolution = 0;
    };

    // Square region of one cascade inside the shadow atlas, in texels.
    struct ShadowAtlasRect
    {
        Int32 X    = 0;
        Int32 Y    = 0;
        Int32 Size = 0;
    };

    // std140 mirror of one element of the `cascades` array in the ShadowData block (PBR.frag).
    // A struct rather than five parallel arrays: a std140 array of scalars has a 16 B stride, so
    // the parallel form either wasted three quarters of every entry or packed four cascades into
    // a vec4 — which is exactly what capped the cascade count at 4 before.
    struct ShadowCascadeGPU
    {
        Matrix4 ViewProj;        //  0, 64 B — lightProj * lightView
        // 64, maps this cascade's [0,1] projected coordinates into atlas UV:
        // atlasUV = projCoords.xy * AtlasScaleBias.xy + AtlasScaleBias.zw.
        Vec4    AtlasScaleBias;
        // 80, x = view-space end distance, y = world size of one texel, z = depth bias already
        // converted into this cascade's [0,1] depth, w = unused (std140 padding made visible).
        Vec4    Params;
    };

    static_assert(sizeof(ShadowCascadeGPU) == 96, "ShadowCascadeGPU must match the std140 array stride");

    // std140 mirror of the `ShadowData` uniform block (binding 2) declared in PBR.frag.
    // The cascade array comes first (16 B aligned, 96 B stride) and the scalars are packed at the
    // end, so no padding hides between fields and the C++ offsets match GLSL exactly (asserted
    // below).
    struct ShadowDataGPU
    {
        ShadowCascadeGPU Cascades[kMaxShadowCascades];  //   0, 6 x 96 B
        // 576, 1 / atlas dimensions. One texel step in atlas UV is the same for every cascade —
        // a texel of the atlas IS a texel of whichever cascade owns it — so the PCF radius (in
        // texels) converts to UV with this single value regardless of per-cascade resolution.
        Vec2    InvAtlasSize;
        Int32   CascadeCount;                          // 584, 0 = no directional shadows this frame
        float   ShadowFadeStart;                       // 588, metres; shadows start fading out here
        float   ShadowFadeEnd;                         // 592, metres; fully lit past this distance
        float   CascadeBlendFraction;                  // 596, fraction of a cascade used to cross-fade
        float   NormalBiasScale;                       // 600, normal offset in texels
        float   PcfRadiusTexels;                       // 604, PCF disk radius in texels
        Int32   DebugVisualizeCascades;                // 608, != 0 tints each cascade
        Int32   PcfTapCount;                           // 612, samples in the PCF disk (1 = one hardware tap)
        Int32   PcfRotateSamples;                      // 616, != 0 rotates the disk per pixel

        // --- Contact shadows (screen-space ray march against the depth prepass) ---
        //
        // What a cascade cannot do at any resolution: a cascade texel covers millimetres only
        // within the first metre or two, while these are computed per SCREEN pixel, so their
        // "texel" is the pixel itself. They cover the short range where CSM runs out of density
        // — small detail self-shadowing, contact points — and are meant to be combined with, not
        // to replace, the cascades.
        Int32   ContactShadowSteps;                    // 620, 0 = off. Samples along the ray.
        float   ContactShadowLength;                   // 624, metres of world space to march
        float   ContactShadowThickness;                // 628, metres; how deep an occluder is assumed to be
        float   ContactShadowBias;                     // 632, metres; keeps a surface off its own ray
        float   Padding;                               // 636, std140 rounds the block up to a multiple of 16
    };

    static_assert(sizeof(ShadowDataGPU) == 640, "ShadowDataGPU must match the std140 ShadowData block");
    static_assert(offsetof(ShadowDataGPU, InvAtlasSize) == 576);
    static_assert(offsetof(ShadowDataGPU, CascadeCount) == 584);
    static_assert(offsetof(ShadowDataGPU, DebugVisualizeCascades) == 608);
    static_assert(offsetof(ShadowDataGPU, PcfTapCount) == 612);
    static_assert(offsetof(ShadowDataGPU, PcfRotateSamples) == 616);
    static_assert(offsetof(ShadowDataGPU, ContactShadowSteps) == 620);
    static_assert(offsetof(ShadowDataGPU, ContactShadowLength) == 624);
    static_assert(offsetof(ShadowDataGPU, ContactShadowThickness) == 628);
    static_assert(offsetof(ShadowDataGPU, ContactShadowBias) == 632);

    // --- Spot lights ---

    // Hard cap on spot lights reaching the GPU in one frame. Sizes the SSBO (binding 5); lights
    // past it are dropped on MAIN by importance, so the ones that survive are the ones that
    // matter. Unrelated to the shadow budget below — a light can be lit without being shadowed.
    constexpr Int32 kMaxVisibleSpotLights = 64;
    // Layers of the spot shadow atlas, i.e. how many spot lights may cast a shadow in one frame.
    constexpr Int32 kMaxSpotShadowSlots   = 8;
    // Per-slot resolution. 512² x 8 layers of D32F is ~32 MB — a fixed cost paid once, which is
    // the point of a fixed pool: shadow VRAM does not grow with the number of lights in a scene.
    constexpr Int32 kSpotShadowResolution = 512;
    // Near plane of a spot's shadow projection, in metres. Close enough to the cone apex that no
    // real caster falls in front of it, which matters because the spot depth pass deliberately
    // does NOT enable GL_DEPTH_CLAMP (see Renderer::RenderSpotShadowPass).
    constexpr float kSpotShadowNearClip   = 0.05f;

    // std430 mirror of the `SpotLightGPU` struct in the `SpotLights` buffer (binding 5) declared
    // in PBR.frag. std430 aligns a vec3 to 16 B and lets the following scalar fill the gap, which
    // is exactly how the (vec3, float) pairs below are packed — no padding hides between them.
    struct SpotLightGPU
    {
        Matrix4 ShadowViewProj;            //   0, 64 B — identity when the light got no slot
        Vec3    Position;                  //  64
        float   Range;                     //  76, metres
        Vec3    Direction;                 //  80, direction of travel
        float   InnerConeCos;              //  92
        Vec3    Color;                     //  96, colour * intensity, premultiplied on MAIN
        float   OuterConeCos;              // 108
        Int32   ShadowSlot;                // 112, -1 = no shadow map this frame
        float   ShadowDepthBias;           // 116, METRES (see ShadowDepthBiasScale)
        float   ShadowNormalBias;          // 120, texels
        float   ShadowPcfRadius;           // 124, texels
        // 128, world size of one shadow texel PER METRE of distance from the light:
        // 2*tan(outerHalfAngle) / resolution. A perspective shadow map's texel grows with
        // distance — the problem CSM does not have — so the receiver-side normal offset has to be
        // scaled by the fragment's distance in the shader instead of by a per-cascade constant.
        float   ShadowTexelWorldPerMetre;
        Int32   ShadowPcfTaps;             // 132
        // 136, converts ShadowDepthBias from metres into this projection's [0,1] depth:
        // bias01 = ShadowDepthBias * ShadowDepthBiasScale / viewDepth².
        //
        // A perspective depth buffer is non-linear (z01 = f/(f-n) * (1 - n/d)), so a CONSTANT
        // bias in [0,1] depth is a constant only at one distance — at 5 m of a 15 m cone it
        // already means ~0.75 m of world offset and the shadow silently stops existing. The
        // exact derivative dz01/dd = f*n/((f-n)*d²) turns that back into a fixed physical
        // distance, which is what "2 cm of bias" has to mean everywhere in the cone.
        // Numerator f*n/(f-n) is per-light and constant, so it is precomputed here.
        float   ShadowDepthBiasScale;
        float   Padding;                   // 140, std430 rounds the struct to a multiple of 16
    };

    static_assert(sizeof(SpotLightGPU) == 144, "SpotLightGPU must match the std430 SpotLights element");
    static_assert(offsetof(SpotLightGPU, ShadowDepthBiasScale) == 136);
    static_assert(offsetof(SpotLightGPU, Position) == 64);
    static_assert(offsetof(SpotLightGPU, Direction) == 80);
    static_assert(offsetof(SpotLightGPU, Color) == 96);
    static_assert(offsetof(SpotLightGPU, ShadowSlot) == 112);
    static_assert(offsetof(SpotLightGPU, ShadowTexelWorldPerMetre) == 128);
    static_assert(offsetof(SpotLightGPU, ShadowPcfTaps) == 132);

    // std140 mirror of the `SpotLightData` uniform block (binding 4) in PBR.frag.
    //
    // Offset/Count are the whole investment in clustered forward: today the index buffer
    // (binding 6) is one global list and Offset is 0, so the shader walks every visible light.
    // Adding clusters later only changes how these two are computed — the loop in PBR.frag does
    // not change at all.
    struct SpotLightDataGPU
    {
        Int32 SpotLightOffset       = 0;  //  0, where this draw's index list starts
        Int32 SpotLightCount        = 0;  //  4, 0 = no spot lights this frame
        float InvSpotShadowResolution = 0.0f;  // 8, 1 / atlas slot resolution
        Int32 SpotShadowSlotCount   = 0;  // 12, 0 = spot shadows unavailable this frame
    };

    static_assert(sizeof(SpotLightDataGPU) == 16, "SpotLightDataGPU must match the std140 SpotLightData block");

    // Bounding sphere of a cone, for culling a spot light against the camera frustum. Much
    // tighter than a Range-radius sphere around the apex, and this is the ONLY test deciding
    // whether a light is sent to the GPU at all, so the tightness is the whole saving.
    //
    // Two cases, because the smallest enclosing sphere changes character at 45°: for a narrow
    // cone (half-angle <= 45°) the sphere touches the apex and the base rim, centred on the axis
    // at Range / (2*cos²θ); for a wide one the base circle itself is the widest part, so the
    // sphere is centred at Range*cosθ with radius Range*sinθ.
    PLURENDER_API void ComputeSpotBoundingSphere(const Vec3& Apex, const Vec3& Dir, float Range,
                                           float HalfAngleRadians, Vec3& OutCenter, float& OutRadius);

    // Light-space matrix of a spot's shadow map: a square perspective projection covering the
    // full cone, from the apex along Dir, with far = Range. The "up" vector is picked as the
    // world axis least parallel to Dir so lookAt does not degenerate for a light shining
    // straight down — the common case for a lamp.
    PLURENDER_API Matrix4 ComputeSpotLightMatrix(const Vec3& Apex, const Vec3& Dir, float Range,
                                           float OuterHalfAngleRadians);

    // Same matrix, handing back the view and projection it is built from — the shadow pass needs
    // them separately for the same reason ShadowCascadeData carries them (see there).
    PLURENDER_API Matrix4 ComputeSpotLightMatrix(const Vec3& Apex, const Vec3& Dir, float Range,
                                           float OuterHalfAngleRadians, Matrix4& OutView, Matrix4& OutProj);

    // Appends a cone wireframe (base circle + Segments spokes from the apex) to an interleaved
    // pos(3)+color(3) line buffer — the same format the physics debug renderer packs into, so it
    // renders through the existing debug-line pass with no new shader and no new renderer code.
    // HalfAngleRadians is clamped to MaxHalfAngleRadians — just under 90° by default, the widest a
    // spot light gets. Pass up to pi for a cone wider than a hemisphere (particle launch cones).
    PLURENDER_API void AppendConeWireframe(DynamicArray<float>& OutLineVerts, const Vec3& Apex, const Vec3& Dir,
                                     float Range, float HalfAngleRadians, const Vec3& Color, Int32 Segments = 24,
                                     float MaxHalfAngleRadians = glm::half_pi<float>() - 0.01f);

    // Appends a sphere wireframe — three axis-aligned great circles — in the same interleaved
    // pos(3)+color(3) line format as AppendConeWireframe.
    PLURENDER_API void AppendSphereWireframe(DynamicArray<float>& OutLineVerts, const Vec3& Center, float Radius,
                                       const Vec3& Color, Int32 Segments = 32);

    // Upper bound on PCF samples per cascade, mirrored by MAX_PCF_TAPS in PBR.frag. The disk is
    // generated analytically (Vogel), so this only caps the shader loop — there is no sample table.
    constexpr Int32 kMaxShadowPcfTaps = 32;

    // Upper bound on contact-shadow ray samples, mirrored by MAX_CONTACT_STEPS in PBR.frag.
    // Every step is a depth-texture fetch per lit pixel, so this caps the one part of the shadow
    // budget that scales with screen resolution rather than with the number of cascades.
    constexpr Int32 kMaxContactShadowSteps = 64;

    // Number of PCF taps that just covers a disk of PcfRadiusTexels without leaving gaps.
    // One tap is a hardware 2x2 comparison, i.e. it already averages ~1 texel², so a disk of
    // area pi*r² texels needs ceil(pi*r²) of them — below that the individual taps show up as
    // rings on a soft edge, above it every extra tap re-reads what a neighbour already covered.
    // Clamped to [1, kMaxShadowPcfTaps]; radius 0 collapses the filter to a single tap anyway.
    Int32 ComputeAutoPcfTapCount(float PcfRadiusTexels);

    // Per-frame configuration of the directional cascade set. Comes from the
    // DirectionalLight shadow settings carried in the render snapshot.
    struct CascadeConfig
    {
        Int32 CascadeCount   = 4;
        float ShadowDistance = 150.0f;  // metres; cascades cover [nearClip, ShadowDistance]
        float SplitLambda    = 0.9f;    // 0 = uniform split, 1 = logarithmic split
        Int32 Resolution     = 2048;    // shadow map resolution of the NEAREST cascade
        Int32 ResolutionFalloff = 2;    // halve the resolution every N cascades (0 = uniform)
    };

    // Resolution of every cascade: cascade i gets Resolution >> (i / Falloff), floored at
    // kMinCascadeResolution. Falloff <= 0 gives every cascade Config.Resolution.
    //
    // Why the far cascades should not keep the near cascade's resolution: a cascade's texel
    // covers 2*Radius / Resolution metres, and Radius grows roughly with the square of the split
    // distance. At the defaults the last cascade's texel is already ~20 cm — halving its
    // resolution costs detail nobody can resolve at that distance, and buys the near cascades
    // their texels back. This is the whole point of a mixed-resolution atlas.
    //
    // Writes Count entries into OutResolutions, which must have room for them.
    PLURENDER_API void ComputeCascadeResolutions(Int32 BaseResolution, Int32 Count, Int32 Falloff, Int32* OutResolutions);

    // Packs the per-cascade squares into one atlas and reports its dimensions.
    //
    // Shelf packing with the atlas width fixed to the LARGEST cascade: for the non-increasing run
    // of powers of two that ComputeCascadeResolutions produces, every shelf is filled exactly
    // (2^k tiles of side W/2^k span W), so the layout wastes nothing but the tail of the last
    // shelf. A square power-of-two atlas would waste far more — {2048,2048,1024,1024,512} packs
    // into 2048x5632 (93% used) but needs 4096² (64% used) if forced square.
    //
    // Resolutions must be non-increasing. Writes Count rects into OutRects.
    PLURENDER_API void BuildShadowAtlasLayout(const Int32* Resolutions, Int32 Count,
                                        ShadowAtlasRect* OutRects, Int32& OutWidth, Int32& OutHeight);

    DynamicArray<Vec3> GetFrustumCornersWorldSpace(const Matrix4& proj, const Matrix4& view);

    // Non-allocating variant of the above — writes the 8 corners into OutCorners.
    // Used on the cascade path, which must not allocate per frame.
    void GetFrustumCornersWorldSpace(const Matrix4& proj, const Matrix4& view, Vec3 OutCorners[8]);

    // --- Cascaded Shadow Maps ---

    // Tworzy macierz projekcji perspektywicznej dla pod-frustum jednej kaskady
    // (te same fov/aspect co kamera główna, ale inny near/far).
    Matrix4 GetCascadeProjectionMatrix(float fovYRadians, float aspect, float nearPlane, float farPlane);

    // Splits [NearClip, Config.ShadowDistance] into Config.CascadeCount view-space distances
    // (practical split scheme; SplitLambda blends logarithmic and uniform). Writes into
    // OutSplits, which is cleared but keeps its capacity — no per-frame allocation.
    void ComputeCascadeSplits(const CascadeConfig& Config, float NearClip, DynamicArray<float>& OutSplits);

    // Builds the light matrices for every cascade. Writes into OutCascades (cleared, capacity
    // kept — no per-frame allocation). Splits must hold Config.CascadeCount ascending distances
    // (i.e. the output of ComputeCascadeSplits).
    //
    // Stability: the light basis is fixed (it looks along LightDir from a fixed origin, nothing
    // camera-derived), the sub-frustum bounding-sphere radius is rounded up to a 1/16 m grid so
    // it stops pulsing, and the cascade centre is snapped to that cascade's texel grid inside
    // the fixed basis. Together these keep shadow edges pixel-stable while the camera moves —
    // a snap performed in a camera-derived basis (the old code) is a mathematical no-op.
    //
    // No near margin is added: casters between the light and the cascade are handled by
    // GL_DEPTH_CLAMP ("pancaking") in the shadow pass, which costs no depth precision.
    //
    // PerCascadeResolutions: resolution of each cascade (see ComputeCascadeResolutions). It feeds
    // the texel snap and TexelWorldSize, so it must be the SAME array the atlas was laid out
    // with — a mismatch makes the stabilising snap land on the wrong grid. Null = Config.Resolution
    // everywhere.
    void ComputeCascadeMatrices(
        const Matrix4& CameraView,
        float FovYRadians, float Aspect,
        float NearClip,
        const Vec3& LightDir,
        const CascadeConfig& Config,
        const DynamicArray<float>& Splits,
        DynamicArray<ShadowCascadeData>& OutCascades,
        const Int32* PerCascadeResolutions = nullptr);

    // --- Frustum culling ---

    struct Frustum { Vec4 Planes[6]; };  // (nx,ny,nz,d), wewnątrz gdy dot(n,p)+d >= 0

    PLURENDER_API Frustum ExtractFrustumPlanes(const Matrix4& viewProj);  // Gribb-Hartmann, znormalizowane
    PLURENDER_API bool    SphereInFrustum(const Frustum& frustum, const Vec3& center, float radius);

    // Same test, ignoring the near plane. This is the correct one for culling shadow casters
    // against a cascade: the shadow pass renders with GL_DEPTH_CLAMP, so a caster in front of
    // the ortho near plane is pancaked ONTO it and still occludes. Testing the near plane would
    // cull exactly the objects between the light and the scene — the ones casting the shadows.
    PLURENDER_API bool    SphereInFrustumNoNear(const Frustum& frustum, const Vec3& center, float radius);
}

#endif //PLUENGINE_RENDERUTILS_H
