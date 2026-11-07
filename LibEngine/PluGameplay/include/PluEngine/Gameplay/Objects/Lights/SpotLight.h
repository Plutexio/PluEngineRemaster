//
// Created by Plutex on 8/8/26.
//

#ifndef PLUENGINE_SPOTLIGHT_H
#define PLUENGINE_SPOTLIGHT_H
#include "LightBaseObject.h"
#include "PluEngine/Core.h"
#include "SpotLight.generated.h"

namespace Plu
{
    // Cone light. Unlike DirectionalLight there may be any number of these in a scene — they are
    // culled against the camera frustum on MAIN and travel to the render thread as an array
    // (RenderSnapshot::SpotLights). Position is GetObjectLocation(), direction is
    // GetObjectForwardVector(); colour and intensity come from LightBaseObject.
    //
    // Shadow maps are a fixed pool of Plu::kMaxSpotShadowSlots atlas layers handed out per frame
    // by importance, so CastShadows is a request, not a guarantee — a light that loses the
    // competition still lights the scene, it just stops occluding.
    PLU_CLASS(PyExport)
    class PLUGAMEPLAY_API SpotLight : public LightBaseObject
    {
        REFLECTION_BODY_SPOTLIGHT()
    public:
        SpotLight() = default;
        virtual ~SpotLight() override = default;

        // Range in metres (engine scale: 1 unit = 1 m). Past it the light contributes nothing —
        // it is both the falloff window and the shadow projection's far plane.
        PLU_PROPERTY(PyExport)
        float Range = 15.0f;

        // Half-angles in degrees. Full brightness inside Inner, smoothstepped to zero at Outer.
        // Outer is the one that sizes the shadow projection's FOV.
        PLU_PROPERTY(PyExport)
        float InnerConeAngle = 20.0f;
        PLU_PROPERTY(PyExport)
        float OuterConeAngle = 35.0f;

        PLU_PROPERTY(PyExport)
        bool CastShadows = true;

        // --- Receiver-side bias ---
        // BOTH DEFAULT TO ZERO, which is not an oversight: on the spot path the caster-side
        // budget already covers acne on its own. The depth pass renders with front-face culling
        // (only back faces reach the map, so the self-shadowing threshold sits on the geometry's
        // hidden side; lit faces are never compared against themselves). Note: before 2026-10-06
        // the culling was inverted by the clockwise mesh winding and a slope-scaled polygon offset
        // was doing the work instead — the zero defaults were measured under that setup.
        //
        // Every positive value here is therefore pure loss: it erodes the shadow near the point
        // where caster and receiver meet, which reads as "the shadow does not reach". The
        // cascades default these to non-zero because a cascade texel at 150 m is ~7 cm, an order
        // of magnitude coarser than a 1024² map spanning a few metres — the same numbers do not
        // transfer. Raise these only if a specific scene actually shows acne (grazing angles or
        // single-sided geometry are the usual culprits).

        // Depth bias in METRES of world space — same unit as DirectionalLight::ShadowDepthBias,
        // so "1 cm of bias" means the same thing on both.
        //
        // It has to be metres precisely BECAUSE a spot map is a perspective projection: its depth
        // buffer is non-linear (z01 = f/(f-n) * (1 - n/d)), so a bias expressed in projected depth
        // is a fixed physical distance at exactly one range and grows quadratically past it — at
        // 0.0015 projected depth on a 15 m cone that is ~3 cm at 1 m but ~1.9 m at 8 m. The
        // renderer ships f*n/(f-n) per light (SpotLightGPU::ShadowDepthBiasScale) and the shader
        // divides by the fragment's squared view depth, keeping this value physical across the
        // whole cone. Note the in-shader slope scaling reaches 11x at grazing angles, so 1 cm
        // here can cost over 10 cm of shadow reach.
        PLU_PROPERTY(PyExport)
        float ShadowDepthBias = 0.0f;

        // Normal offset in TEXELS of the shadow map. A perspective map's texel grows with
        // distance from the light, so the renderer ships the per-metre texel size and the shader
        // scales this by the receiver's distance — one value stays correct across the whole cone.
        PLU_PROPERTY(PyExport)
        float ShadowNormalBias = 0.0f;

        // Radius of the PCF disk, in texels. 0 collapses the filter to a single hardware 2x2 tap.
        PLU_PROPERTY(PyExport)
        float ShadowPcfRadius = 1.5f;

        // Samples taken across the PCF disk, 1..Plu::kMaxShadowPcfTaps (32).
        PLU_PROPERTY(PyExport)
        Int32 ShadowPcfTaps = 8;

        // Manual override of the atlas slot competition. Higher wins; ties break on importance
        // (intensity over squared distance to the camera). Raise it on the one light whose
        // shadow a scene is actually about, so it never loses its slot to a passing lamp.
        PLU_PROPERTY(PyExport)
        Int32 ShadowPriority = 0;
    };
}

#endif //PLUENGINE_SPOTLIGHT_H
