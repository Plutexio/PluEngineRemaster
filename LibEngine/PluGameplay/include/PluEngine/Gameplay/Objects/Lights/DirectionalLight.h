//
// Created by Plutex on 4/19/26.
//

#ifndef PLUENGINE_DIRECTIONALLIGHT_H
#define PLUENGINE_DIRECTIONALLIGHT_H
#include "LightBaseObject.h"
#include "PluEngine/Core.h"
#include "DirectionalLight.generated.h"

namespace Plu
{
    PLU_CLASS(PyExport)
    class PLUGAMEPLAY_API DirectionalLight : public LightBaseObject
    {
        REFLECTION_BODY_DIRECTIONALLIGHT()
    public:
        DirectionalLight() = default;
        virtual ~DirectionalLight() override = default;

        // --- Cascaded shadow map settings ---
        // Copied into the render snapshot each frame (DirectionalLightShadowSettings) and clamped
        // by the renderer, which rebuilds its GL resources when the resolution or cascade count
        // changes. Editing them live in the details panel takes effect on the next frame.

        PLU_PROPERTY(PyExport)
        bool CastShadows = true;

        // Metres. The cascades cover [camera near clip, ShadowDistance]; past it shadows fade out.
        // 150 m puts the last cascade's texel at ~7 cm (at 2048²) instead of the ~15 cm you get at
        // 300 m — raise it per scene when a level actually needs shadows that far out.
        PLU_PROPERTY(PyExport)
        float ShadowDistance = 150.0f;

        // 1..Plu::kMaxShadowCascades. More cascades = finer texels up close, one extra depth pass each.
        PLU_PROPERTY(PyExport)
        Int32 ShadowCascadeCount = 4;

        // 0 = uniform split (equal slices), 1 = logarithmic (much denser near the camera).
        PLU_PROPERTY(PyExport)
        float ShadowSplitLambda = 0.9f;

        // Shadow map resolution of the NEAREST cascade; clamped to one of 512 / 1024 / 2048 /
        // 4096 / 8192. The others follow ShadowResolutionFalloff below. This is the primary lever
        // for sharp shadows — a shadow that still looks blocky with a tight PCF radius has texels
        // larger than screen pixels, and only more texels fix that.
        PLU_PROPERTY(PyExport)
        Int32 ShadowResolution = 2048;

        // Halve the shadow map resolution every N cascades: cascade i renders at
        // ShadowResolution >> (i / N), floored at 256. 0 = every cascade at ShadowResolution.
        //
        // A far cascade covers tens of metres, so its texel is already decimetres wide — spending
        // the near cascade's resolution there resolves nothing while eating the whole atlas. At
        // the default (4 cascades, 2048) this turns a 67 MB / 16.8 MPix shadow pass into
        // 42 MB / 10.5 MPix with no visible change close up, which is where the budget belongs.
        PLU_PROPERTY(PyExport)
        Int32 ShadowResolutionFalloff = 2;

        // Receiver-side normal offset, in texels of the cascade. Raise it to kill acne on grazing
        // surfaces, at the cost of shadows detaching from their caster (peter-panning).
        PLU_PROPERTY(PyExport)
        float ShadowNormalBias = 1.2f;

        // Receiver-side depth bias, in METRES of world space. Constant physical distance in every
        // cascade — the renderer converts it into each cascade's depth range. Zero by default:
        // the shadow pass culls front faces, so lit surfaces never compare against their own depth
        // and the normal offset alone keeps the terminator clean; any depth bias here shows up as
        // a gap at the foot of objects, where the back faces in the map sit on the ground.
        PLU_PROPERTY(PyExport)
        float ShadowDepthBias = 0.0f;

        // Radius of the PCF disk, in texels. Larger = softer, blurrier shadow edges; 0 collapses
        // the filter to a single hardware 2x2 tap, which is the hardest edge the map can produce.
        PLU_PROPERTY(PyExport)
        float ShadowPcfRadius = 1.5f;

        // Derive the tap count from ShadowPcfRadius instead of using ShadowPcfTaps. There is
        // exactly one correct value for a given radius — ceil(pi * radius²), see
        // Plu::ComputeAutoPcfTapCount — so leaving this on is the sane default: no banding from
        // under-sampling a wide disk, no taps wasted re-reading the same texels on a tight one.
        // Turn it off only to profile or to force a specific sample budget.
        PLU_PROPERTY(PyExport)
        bool ShadowPcfAutoTaps = true;

        // Samples taken across the PCF disk, 1..Plu::kMaxShadowPcfTaps (32). One tap = no disk at
        // all. IGNORED while ShadowPcfAutoTaps is on. Raise it together with the radius: too few
        // samples over a wide radius shows the individual taps as banding.
        PLU_PROPERTY(PyExport)
        Int32 ShadowPcfTaps = 8;

        // Rotate the sample disk per pixel (interleaved gradient noise). Without it the filter
        // pattern is identical everywhere, so a small radius leaves the shadow edge stepping along
        // the cascade's texel grid — visible as blocky stairs. With it those stairs become a
        // one-pixel dither, which is what makes a sharp shadow read as sharp instead of pixelated.
        // Cost is the rotation only; turn it off to get back the old fully deterministic pattern.
        PLU_PROPERTY(PyExport)
        bool ShadowPcfRotate = true;

        // Fraction of each cascade used to cross-fade into the next one (0 = hard seam).
        PLU_PROPERTY(PyExport)
        float ShadowCascadeBlend = 0.15f;

        // --- Contact shadows ---
        //
        // Short-range shadows ray-marched through the depth prepass, per screen pixel. They exist
        // because a cascade cannot resolve millimetres: its texel covers 2*Radius/Resolution
        // metres and grows with the square of the split distance, so detail below a couple of
        // centimetres stops casting long before the cascade runs out of range. A contact shadow's
        // "texel" is the screen pixel, so it stays sharp at any distance — but only for the short
        // ray it can afford to march.
        //
        // Intended split of labour: cascades carry the scene, these carry the detail. That is why
        // raising ShadowDistance (and letting the near cascades go coarse) is now a reasonable
        // trade rather than a regression.
        //
        // Requires CastShadows: that flag is the light's shadow switch as a whole, not just its
        // cascade switch, so turning it off also skips the depth prepass entirely — contact
        // shadows are its only consumer today, so the pass would be pure cost.
        PLU_PROPERTY(PyExport)
        bool ContactShadows = true;

        // Metres of world space the ray covers. Keep it SHORT — this is the distance over which
        // a detail may occlude, not a shadow range. Beyond ~0.5 m the march either steps over
        // thin geometry or costs too many samples to be worth it; the cascades take over there.
        PLU_PROPERTY(PyExport)
        float ContactShadowLength = 0.25f;

        // Samples along the ray, 4..64. Cost is this many depth-texture fetches per lit pixel, so
        // it is the one real performance knob here.
        //
        // Samples are spaced QUADRATICALLY, not evenly: at 16 steps over 0.25 m the first lands
        // ~1 mm out and the next few every 3-7 mm, which is the scale detail actually casts at,
        // while the far ones stretch to centimetres and keep the range. Even spacing put the
        // whole ray at one resolution (25 cm / 12 = 2 cm) — coarser than the detail it was meant
        // to catch, so the ray stepped straight over it and contact shadows barely showed.
        PLU_PROPERTY(PyExport)
        Int32 ContactShadowSteps = 16;

        // Assumed depth of an occluder, in metres. A depth buffer stores one surface, not solids,
        // so a hit is only believed when the ray is within this distance BEHIND it. Too large and
        // distant background geometry shadows the foreground; too small and genuine occluders are
        // missed at grazing angles.
        PLU_PROPERTY(PyExport)
        float ContactShadowThickness = 0.05f;

        // Offset of the ray's origin along its own direction, in metres. Keeps a surface from
        // hitting itself in the first sample, which would otherwise darken every lit pixel.
        //
        // Keep it SMALLER than the detail you want shadowed — it is dead distance at the start of
        // every ray. A centimetre of bias means a centimetre-tall detail is already behind the
        // first sample, which is exactly how you get contact shadows that technically run and
        // visibly do nothing.
        PLU_PROPERTY(PyExport)
        float ContactShadowBias = 0.002f;
    };
}

#endif //PLUENGINE_DIRECTIONALLIGHT_H
