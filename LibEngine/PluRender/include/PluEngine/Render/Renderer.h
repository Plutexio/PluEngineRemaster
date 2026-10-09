//
// Created by Plutex on 6/22/26.
//

#ifndef PLUENGINE_RENDERER_H
#define PLUENGINE_RENDERER_H

#include <atomic>

#include "GLFrameBuffer.h"
#include "GLSamplerObject.h"
#include "GLShaderStorageBuffer.h"
#include "GLUniformBuffer.h"
#include "ParticlePointBuffer.h"
#include "ParticleInstanceBuffer.h"
#include "PluEngine/Effects/Particles/ParticleEmitterInstance.h"
#include "HashSet/HashSet.h"
#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "RenderThreading.h"
#include "RenderUtils.h"

namespace Plu
{
    class ParticleSpawner;
    struct ParticleDebugStats;
    struct ParticleSystemSpawnerDebugStats;
    class ShaderProgram;
    struct MaterialInfo;
    struct RenderSnapshot;
    struct StaticMesh;
    struct SkeletalMesh;

    //This is a render thread only object
    class Renderer
    {
        // Shadow map resolution of the nearest cascade when nothing has configured one yet.
        // The far cascades step down from it (DirectionalLight::ShadowResolutionFalloff).
        static constexpr Int32 kDefaultShadowResolution = 2048;

        ApplicationInfo* mApplicationInfo;
        TOwningPointer<FrameBuffer> mMainBuffer;

        //Buffer with post-processing applied, used in GetMainBuffer
        TOwningPointer<FrameBuffer> mPostProcessBuffer;

        // --- Depth prepass ---
        //
        // Scene depth rendered before the lighting pass, into its own framebuffer, feeding contact
        // shadows. It cannot be the main buffer's own depth: the ray-march reads it WHILE the
        // lighting pass writes colour into the same framebuffer, and sampling an attachment of the
        // bound framebuffer is a feedback loop — undefined by spec and rejected outright by some
        // drivers.
        //
        // It does NOT feed early-Z yet, though the reason has shrunk: the pass now draws with the
        // materials' own vertex shaders (see Renderer::ResolveDepthProgram), so both passes build
        // gl_Position from the same expression. What is still missing is `invariant gl_Position`
        // in the shaders — without it the spec allows two programs compiled from the same source
        // to differ by an ulp — plus sharing this depth with the main framebuffer. See
        // RenderDepthPrepass.
        //
        // FrameBufferType::DepthOnly, i.e. a D32F texture — precision the contact-shadow ray uses
        // directly, since it linearises this value at every sample.
        TOwningPointer<FrameBuffer> mDepthPrepassBuffer;
        // Observer of the above framebuffer's depth texture — this is what the shaders sample.
        TUsePointer<Texture> mSceneDepthTexture;

        // Texture unit of the scene depth, one below the spot atlas. Engine samplers keep growing
        // DOWN from the last guaranteed unit while material textures grow up from 0 — see
        // kShadowTextureUnit for why that separation matters. Must match `layout(binding = 13)`
        // in PBR.frag.
        static constexpr GLuint kSceneDepthTextureUnit = 13;

        // (Re)builds the prepass framebuffer whenever the main buffer's size stops matching.
        // Cheap no-op on the steady path; called every frame because the main buffer is resized
        // from RenderingManager without notifying anyone.
        void EnsureDepthPrepassBuffer();
        // Pass 0: scene depth only, camera frustum. Runs after the shadow passes because it
        // shares their visible-index SSBO (binding 3) — the camera is simply the last frustum in
        // CullShadowCasters' sweep.
        // view and projection go in separately, not premultiplied: the shaders that draw this pass
        // are material vertex shaders, which take the two the way the lighting pass sets them.
        void RenderDepthPrepass(RenderSnapshot* snapshot, const Matrix4& view, const Matrix4& projection);
        // Same feedback-loop and ImGui reasons as UnbindShadowTexture — see there.
        void UnbindSceneDepthTexture();

        // Whether anything will read the scene depth this frame. ONE place, because the prepass
        // and the UBO must agree: running the pass while the shader ignores its result is a whole
        // geometry pass of pure waste, and the reverse makes the shader march through a buffer
        // nobody refreshed this frame.
        //
        // CastShadows gates this too — it is the light's shadow switch, not just its cascade
        // switch, so turning it off has to leave the light casting nothing at all.
        [[nodiscard]] bool AreContactShadowsActive(const RenderSnapshot* snapshot) const;

        // All cascade depth maps in ONE plain 2D depth texture, side by side.
        //
        // An atlas rather than the GL_TEXTURE_2D_ARRAY this used to be, because an array forces
        // every layer to the same dimensions — which meant the cascade covering 30-150 m paid the
        // same texels as the one covering the first two metres. A cascade's texel already grows
        // with the square of its split distance, so uniform resolution spends most of the budget
        // where nothing can resolve it. With an atlas each cascade brings its own size and the
        // shader reaches it through a scale/bias into UV space (ShadowCascadeGPU::AtlasScaleBias).
        //
        // Observer, not owner: the depth texture belongs to the framebuffer below (it was created
        // as FrameBufferType::DepthOnly), so destroying that framebuffer is the one and only way
        // the atlas is released.
        TUsePointer<Texture> mShadowAtlas;
        // The single depth-only framebuffer covering the whole atlas. Cascades are separated by
        // glViewport, not by attachment — one bind and one clear for the entire shadow pass
        // instead of one framebuffer object per cascade.
        TOwningPointer<FrameBuffer> mShadowAtlasFrameBuffer;
        // Comparison sampler bound over the shadow atlas during the lighting pass only — that is
        // what makes `texture()` do hardware PCF. It must be unbound before ImGui runs (a
        // comparison sampler on a colour texture renders it black), see the end of RenderSnapshot.
        SamplerObject mShadowCompareSampler;
        // Texture unit reserved for the shadow atlas. Deliberately the LAST unit guaranteed by
        // GL 4.5 (GL_MAX_TEXTURE_IMAGE_UNITS >= 16), not unit 0, and material textures keep
        // starting at 0 (SetSlotsUsed stays 0).
        //
        // Reason: RenderFromMaterial only calls SetTextureUniform for samplers that actually got
        // a texture, so a material sampler with nothing assigned keeps GLSL's default unit 0.
        // Two sampler uniforms of DIFFERENT types on the same unit inside one program is
        // INVALID_OPERATION at draw time — some drivers ignore it, Mesa rejects the draw and the
        // scene goes black. Parking the shadow array at the far end removes the whole class.
        // Must match `layout(binding = 15)` in PBR.frag.
        static constexpr GLuint kShadowTextureUnit = 15;

        // Releases the shadow unit back to plain, unbound state (texture AND sampler).
        //
        // Must be called before the depth pass renders INTO the atlas: leaving it bound for
        // sampling while it is the render target is a framebuffer feedback loop — undefined by
        // spec, tolerated by some drivers and rejected with INVALID_OPERATION by others (Mesa),
        // which then leaves the cascade maps unwritten and the whole scene reading as occluded.
        // Also called at the end of the frame, because a comparison sampler left on unit 0
        // renders every ImGui texture black.
        void UnbindShadowTexture();

        // Current GL-side geometry of the shadow atlas, so a frame can detect that the settings
        // changed and rebuild. -1 = nothing created yet.
        Int32 mShadowAtlasWidth  = -1;
        Int32 mShadowAtlasHeight = -1;
        // GL_MAX_TEXTURE_SIZE, read once in Initialize. The atlas grows DOWNWARDS (one shelf per
        // cascade or two), so its height is what runs into this limit long before its width does.
        Int32 mMaxTextureSize = 0;

        // This frame's cascade resolutions and their squares inside the atlas, both indexed by
        // cascade. Members (not locals) so the per-frame rebuild reuses their capacity. They are
        // rebuilt together in PrepareShadowCascades and MUST stay consistent: the texel snap in
        // ComputeCascadeMatrices is derived from the resolution, so a rect whose size disagrees
        // with it would put the stabilising snap on a grid the atlas does not have.
        DynamicArray<Int32>           mCascadeResolutions;
        DynamicArray<ShadowAtlasRect> mCascadeAtlasRects;

        // This frame's clamped shadow settings (from the DirectionalLight, via the snapshot).
        DirectionalLightShadowSettings mShadowSettings;
        // True while the depth maps are known to be blank. Turning shadows off clears them once
        // instead of every frame; nothing samples them at CascadeCount = 0 anyway.
        bool mShadowMapsCleared = false;

        // Brings user-authored settings into the range the renderer can actually honour
        // (cascade count, power-of-two resolution, sane biases). One place, so no consumer
        // downstream has to be defensive.
        static DirectionalLightShadowSettings ClampShadowSettings(const DirectionalLightShadowSettings& Settings);

        // (Re)builds the shadow atlas texture and its framebuffer at the given size. No-op when
        // the geometry already matches. Render thread only (it touches GL).
        void RecreateShadowResources(Int32 AtlasWidth, Int32 AtlasHeight);
        void DestroyShadowResources();

        // Fills mCascadeResolutions / mCascadeAtlasRects from the settings and resizes the atlas
        // to match. The two must be derived together — see the members' comment — so this is the
        // only place either is written. Render thread only (it may touch GL).
        void BuildCascadeAtlas(const DirectionalLightShadowSettings& Settings);

        // Cascade state of the current frame, rebuilt by PrepareShadowCascades and consumed by
        // the main pass. Members (not locals) so the per-frame rebuild reuses their capacity —
        // Clear() keeps it, so the steady state allocates nothing. Empty = no shadows this frame.
        DynamicArray<ShadowCascadeData> mCascades;
        DynamicArray<float> mCascadeSplits;

        // --- Spot light shadows ---
        //
        // Texture unit for the spot shadow atlas, one below the cascade array. Same reasoning as
        // kShadowTextureUnit: engine samplers grow DOWN from the last guaranteed unit while
        // material textures grow up from 0, so an unassigned material sampler sitting on its
        // default unit 0 can never collide with a sampler of a different type (which is
        // INVALID_OPERATION at draw time and blanks the pass on Mesa).
        // Must match `layout(binding = 14)` in PBR.frag.
        static constexpr GLuint kSpotShadowTextureUnit = 14;

        // All spot shadow slots in ONE GL_TEXTURE_2D_ARRAY, exactly like the cascades — a light
        // indexes its layer with a per-light variable, which is legal for an array layer and is
        // NOT legal for an array of samplers.
        TOwningPointer<Texture> mSpotShadowArray;
        DynamicArray<TOwningPointer<FrameBuffer>> mSpotShadowFrameBuffers;  // one layer FBO per slot
        // Current GL-side geometry, so Recreate can no-op. -1 = nothing created yet. Unlike the
        // cascades these never change at runtime (they are engine constants, not light settings).
        Int32 mSpotShadowResolution = -1;
        Int32 mSpotShadowSlotCount  = -1;

        // Same feedback-loop and ImGui reasons as UnbindShadowTexture — see there.
        void UnbindSpotShadowTexture();
        // Builds the atlas and its layer framebuffers at kSpotShadowResolution /
        // kMaxSpotShadowSlots. No-op once they exist. Render thread only (touches GL).
        void RecreateSpotShadowResources();
        void DestroySpotShadowResources();

        // Slot assignment of the current frame. mSpotShadowSlotOwners[slot] is an index into
        // snapshot->SpotLights (which arrives sorted by importance, so the assignment is just
        // "walk the front of the list"), and mSpotShadowMatrices[slot] is that light's
        // projection. Both empty = no spot shadows this frame.
        DynamicArray<Int32>   mSpotShadowSlotOwners;
        DynamicArray<Matrix4> mSpotShadowMatrices;
        // The view and projection mSpotShadowMatrices[slot] is the product of. Kept for the same
        // reason ShadowCascadeData::View/Proj are: the depth pass hands them to the shader as its
        // own "view"/"projection" uniforms instead of a premultiplied matrix.
        DynamicArray<Matrix4> mSpotShadowViews;
        DynamicArray<Matrix4> mSpotShadowProjs;
        // Per-slot caster counts for the stats panel, mirroring mCascadeCasterCounts.
        DynamicArray<UInt32>  mSpotShadowCasterCounts;

        // CPU mirrors + GPU buffers of the per-frame light list. Allocated ONCE at
        // kMaxVisibleSpotLights and never resized: MAIN already trims the snapshot to that cap,
        // so a fixed 64-element buffer (9 KB) removes the grow-and-rebind dance the instance
        // buffers need. Members so the scratch arrays keep their capacity between frames.
        DynamicArray<SpotLightGPU> mSpotLightScratch;
        DynamicArray<UInt32>       mSpotLightIndices;
        ShaderStorageBuffer<SpotLightGPU> mSpotLightBuffer;       // binding 5
        ShaderStorageBuffer<UInt32>       mSpotLightIndexBuffer;  // binding 6
        SpotLightDataGPU mSpotLightData;
        UniformBuffer<SpotLightDataGPU> mSpotLightDataBuffer;     // binding 4

        // Picks this frame's shadow slots and their light matrices. CPU only — no draws — because
        // the caster culling below needs the spot frusta before anything is rendered.
        void PrepareSpotShadowSlots(RenderSnapshot* snapshot);
        // Pass 1b: renders caster depth into the atlas slots. Runs after the cascade pass and
        // shares its visible-index SSBO (binding 3).
        void RenderSpotShadowPass(RenderSnapshot* snapshot);
        // Uploads UBO 4 + SSBO 5 + SSBO 6. Called UNCONDITIONALLY every frame (with
        // SpotLightCount = 0 when there are no lights), for the same reason as
        // UpdateShadowDataBuffer: no frame may read the previous frame's light list.
        void UpdateSpotLightBuffers(RenderSnapshot* snapshot);

        // Debug: one atlas slot copied out for display, same mechanism as mShadowLayerViewRequest.
        std::atomic<Int32> mSpotShadowViewRequest{-1};
        TOwningPointer<Texture> mSpotShadowView;
        void UpdateSpotShadowSlotView();

        // The two engine depth shaders, resolved once per frame (ResolveDepthShaders) and shared
        // by the cascade and spot passes. Previously each pass looked them up itself.
        TUsePointer<ShaderProgram> mDepthShader;
        TUsePointer<ShaderProgram> mSkeletalDepthShader;
        bool mSkeletalDepthReady = false;
        // Particle shadow caster program (ParticleShadowProgram), resolved alongside the two above.
        // Independent of them: not ready yet only means no particle shadows this frame.
        TUsePointer<ShaderProgram> mParticleShadowShader;
        bool mParticleShadowReady = false;

        // Depth-pass variants of material shaders: the material's OWN vertex shader linked with
        // the engine's empty fragment shader. The depth prepass and the shadow maps draw with
        // these instead of mDepthShader, so whatever a material does to gl_Position — wind, any
        // vertex animation — lands in the depth buffer exactly as it lands on screen. Without
        // this a waving grass blade was drawn displaced but shadowed (and contact-shadowed) from
        // its rest pose.
        //
        // Keyed by VERTEX SHADER uuid, not by material or program: every material sharing a
        // vertex shader shares one variant, so a scene with thirty materials on
        // BasicVertInstanced.vert compiles one depth program.
        HashMap<UInt64, TOwningPointer<ShaderProgram>> mDepthVariants;
        // Programs already warned about being instanced without the VisibleInstanceIndices block
        // (the pre-variant convention) — one line per program, not one per frame.
        HashSet<UInt64> mWarnedLegacyInstancedPrograms;
        // The variant for this material's program, or null when the material has no usable one
        // (unknown program, not compiled yet, or an instanced shader still on the pre-
        // VisibleInstanceIndices convention) — the caller then falls back to mDepthShader.
        TUsePointer<ShaderProgram> ResolveDepthProgram(MaterialInfo* materialInfo);
        // Drops every depth variant, unregistering it from the shader manager's renderable list
        // first. Render thread only — these own GL programs.
        void DestroyDepthVariants();

        // Draws the static batches of ONE depth frustum (a cascade, a spot slot, or the camera
        // for the prepass), reading that frustum's slice of mShadowDrawRanges. Shared by all three
        // depth passes so they cannot drift apart: each batch draws with its material's depth
        // variant when there is one, with the engine depth shader otherwise, and the frustum's
        // view/projection go in as the shader's own uniforms.
        void DrawStaticDepthBatches(RenderSnapshot* snapshot, UInt32 frustumIndex,
                                    const Matrix4& view, const Matrix4& projection);
        // The skeletal counterpart, one object at a time — the callers keep their own culling and
        // CastsShadow rules, which differ per pass.
        void DrawSkeletalDepthObject(RenderSnapshot* snapshot, UInt32 objectIndex,
                                     const Matrix4& view, const Matrix4& projection);
        // Resolves both, requesting a lazy compile on the render thread when needed. Returns
        // false when the static depth shader is not ready — without it there is no depth pass at
        // all, so every shadow of the frame is skipped (skeletal readiness only skips skinned
        // casters, which is why it is a separate flag).
        bool ResolveDepthShaders();

        // Per-frame shadow parameters shared by every shader that declares the `ShadowData`
        // block (std140, binding 2). CPU-side mirror + the GPU buffer. Uploaded and bound
        // UNCONDITIONALLY every frame — with no directional light it simply carries
        // CascadeCount = 0. That is what makes the old "stale cascade count" class of bug
        // structurally impossible: there is no frame in which shaders read last frame's value.
        ShadowDataGPU mShadowData;
        UniformBuffer<ShadowDataGPU> mShadowDataBuffer;

        // Fills mShadowData from the cascades built this frame (or zeroes the count) and
        // uploads it. Call once per frame, before the main pass.
        void UpdateShadowDataBuffer(RenderSnapshot* snapshot);

        // VAO/VBO debugowej geometrii fizyki (linie + punkty). Tworzone eager w Initialize na
        // wątku renderu; uploadowane per-klatkę z płaskich buforów snapshotu (pos(3)+color(3)).
        unsigned int mDebugVao = 0;
        unsigned int mDebugVbo = 0;

        // Empty VAO for the editor grid fullscreen pass — the draw is attribute-less
        // (EditorGrid.vert builds the triangle from gl_VertexID), but core profile still
        // requires a VAO to be bound. Created eager in Initialize on the render thread.
        unsigned int mGridVao = 0;

        // Meshe snapshotu rozwiązane RAZ na klatkę (ResolveSnapshotMeshes na początku
        // RenderSnapshot); indeks = indeks batcha w StaticMeshBatches / obiektu w
        // SkeletalMeshRenderObjects. GetAssetDataNoLoad bierze shared_lock na mutexie
        // AssetManagera — bez tego cache'u pass cieni brał go per batch PER KASKADA (x5),
        // kontendując z main threadem o ten sam mutex. Membery reużywane między klatkami
        // (Clear zachowuje capacity). Wpisy mogą być nullem (asset jeszcze nie w mapie).
        DynamicArray<TUsePointer<StaticMesh>> mResolvedBatchMeshes;
        DynamicArray<TUsePointer<SkeletalMesh>> mResolvedSkeletalMeshes;
        void ResolveSnapshotMeshes(RenderSnapshot* snapshot);

        // Fills mCascades with the matrices this frame's cascades (and the main pass) need, and
        // rebuilds the GL resources when the settings changed. Leaves mCascades empty when there
        // is no directional light or the depth shader is not ready yet.
        //
        // Split from the drawing below because the caster culling is now shared with the spot
        // shadow slots: every shadow frustum of the frame — cascades AND spot slots — has to be
        // known before a single one of them is culled, so that the visible-index SSBO stays one
        // upload on one binding point.
        void PrepareShadowCascades(RenderSnapshot* snapshot, const Matrix4& cameraView);

        // Pass 1: renders caster depth into the cascade maps. Assumes PrepareShadowCascades and
        // CullShadowCasters already ran this frame.
        void RenderShadowPass(RenderSnapshot* snapshot);

        // Per-cascade caster culling. Runs on the RENDER thread because the cascade frusta only
        // exist here — culling on main would mean duplicating the whole cascade math there.
        //
        // Covers EVERY shadow frustum of the frame in one sweep: the cascades first, then the
        // spot shadow slots, so a range lives at [frustum * batchCount + batch] with
        // frustum < mCascades.Size() meaning "cascade" and the rest meaning "spot slot". One
        // upload, one binding, and the depth shaders stay untouched.
        //
        // The CAMERA frustum is the last entry of that sweep, feeding the depth prepass: it wants
        // exactly the same "which instances survive this frustum" answer, so it rides along
        // instead of duplicating the loop. Index it with CameraFrustumIndex().
        //
        // For every (frustum, batch) pair it walks that batch's instance bounds and compacts the
        // survivors' indices into one concatenated array, uploaded as a single SSBO (binding 3).
        // The depth vertex shader then reads instances[visibleIndices[base + gl_InstanceID]], so
        // a culled batch costs 4 B per surviving instance instead of re-uploading 128 B of
        // instance data — and a batch that survives nowhere is skipped entirely (the draw-call win).
        struct ShadowDrawRange { UInt32 Offset = 0; UInt32 Count = 0; };
        DynamicArray<UInt32> mVisibleInstanceScratch;      // concatenated instance indices
        DynamicArray<ShadowDrawRange> mShadowDrawRanges;   // indexed [frustum * batchCount + batch]
        ShaderStorageBuffer<UInt32> mVisibleInstanceBuffer;
        // Per-cascade caster counts (static instances + skeletal meshes), for the stats panel.
        DynamicArray<UInt32> mCascadeCasterCounts;

        // Fills mVisibleInstanceScratch / mShadowDrawRanges for this frame's cascades, spot slots
        // and the camera, and uploads the buffer. Returns the number of instance-frustum pairs
        // culled away by the SHADOW frusta (for stats) — the camera's own culling is already
        // counted on MAIN, so counting it here would double it.
        UInt32 CullShadowCasters(RenderSnapshot* snapshot);

        // Index of the camera frustum inside mShadowDrawRanges (it is appended after every
        // cascade and every spot slot). Valid only after CullShadowCasters ran this frame.
        [[nodiscard]] UInt32 CameraFrustumIndex() const
        {
            return mCascades.Size() + mSpotShadowMatrices.Size();
        }

        // Debug: one cascade copied out of the atlas for display. The atlas itself is a plain
        // GL_TEXTURE_2D that ImGui could bind directly, but showing it whole would render every
        // cascade at once — the viewer wants one, at its own resolution — so the render thread
        // blits the requested cascade's rect into a private texture with glCopyImageSubData.
        // The request is an atomic written by the UI thread (-1 = nobody is looking).
        std::atomic<Int32> mShadowLayerViewRequest{-1};
        TOwningPointer<Texture> mShadowLayerView;
        void UpdateShadowLayerView();

        // Rysuje debugową geometrię fizyki ze snapshotu (linie + punkty) shaderem DebugLine.
        void RenderDebugGeometry(RenderSnapshot* snapshot, const Matrix4& viewProj);

        // Draws the infinite procedural editor grid (EditorGrid shader, Y=0 plane) as a
        // fullscreen pass blended over the scene. Depth test stays on (the shader writes the
        // plane's real depth), depth writes stay off — the grid never occludes anything.
        void RenderEditorGrid(RenderSnapshot* snapshot, const Matrix4& view);

        //Skeletal sTUFF
        ShaderStorageBuffer<Matrix4> mSkeletalMatricesBuffer;

        // Palety skinningu (skin = global * offset) WSZYSTKICH skeletal meshy klatki, liczone
        // raz w RenderSnapshot (BuildSkeletalPalettes) — dawniej pass cieni przeliczał je per
        // kaskada × obiekt (z alokacją), a główny pass jeszcze raz. Płaski scratch + zakresy
        // per obiekt (indeks = indeks w snapshot->SkeletalMeshRenderObjects); tablice są
        // memberami reużywanymi między klatkami (Clear zachowuje capacity — zero alokacji
        // w ustalonym stanie).
        struct SkeletalPaletteRange { UInt32 Offset = 0; UInt32 Count = 0; };
        DynamicArray<Matrix4> mSkeletalPaletteScratch;
        DynamicArray<SkeletalPaletteRange> mSkeletalPaletteRanges;

        // Liczy palety do scratcha (patrz wyżej). Wołane raz na klatkę, przed RenderShadowPass.
        void BuildSkeletalPalettes(RenderSnapshot* snapshot);

        // Uploads EVERY palette of the frame to mSkeletalMatricesBuffer (binding 0) in one go and
        // binds it for the whole frame; draws then just set `paletteBaseIndex` to their range's
        // offset. Previously each object rewrote the whole shared buffer, once per cascade plus
        // once for the main pass — the same data uploaded N x (cascades + 1) times per frame.
        void UploadSkeletalPalettes();

        // Dane instancji static meshy (SSBO binding 1 — binding 0 to BoneMatrices skeletal, patrz
        // HELPERS.md). Bufor bindowany w CAŁOŚCI (BindBase) na całą klatkę w RenderSnapshot();
        // batche adresują swój zakres uniformem "instanceBaseIndex", nie BindRange per batch
        // (offsety BindRange muszą być wielokrotnością GL_SHADER_STORAGE_BUFFER_OFFSET_ALIGNMENT,
        // które u części sterowników wynosi 256 — sizeof(InstanceGPUData)==128 by tego nie
        // gwarantowało). Skeletal używa osobnego indeksowanego punktu 0 (BoneMatrices), więc
        // bufor instancji przeżywa całą klatkę bez potrzeby rebindu.
        ShaderStorageBuffer<InstanceGPUData> mInstanceBuffer;

        // Programy shaderowe, dla których już ostrzegliśmy, że skeletal mesh jest rysowany bez
        // bloku SSBO "BoneMatrices" (mesh zamarza w bind pose) — warning raz, nie per klatkę.
        HashSet<UInt64> mWarnedNonSkeletalPrograms;

        // Programy shaderowe, dla których już ostrzegliśmy, że batch instancji (VisibleCount > 1)
        // renderuje się fallbackiem per-obiekt, bo shader nie ma bloku SSBO "InstanceMatrices" —
        // warning raz, nie per klatkę (analogicznie do mWarnedNonSkeletalPrograms).
        HashSet<UInt64> mWarnedNonInstancedPrograms;

        // GPU side of one emitter of an asset-driven spawner. Which buffers are filled follows the emitter's
        // renderer modules: Sprite -> SpriteInstances; Ribbon -> RibbonInstances (+ History for per-particle
        // trails); both may be in use at once; neither -> Points. The two instance buffers hold the same
        // particles with different field meanings (ParticleInstanceGPU). Plain handles, freed by
        // DestroyRenderParticleSpawner.
        struct RenderParticleEmitterGPU
        {
            ParticlePointBuffer Points;
            ParticleInstanceBuffer SpriteInstances;
            ParticleInstanceBuffer RibbonInstances;
            ParticleRibbonHistoryBuffer History;
            // Mean position of the live particles, from the packing loop: sorting key of the transparent pass.
            Vec3 Centroid = Vec3(0.0f);
            void Destroy() { Points.Destroy(); SpriteInstances.Destroy(); RibbonInstances.Destroy(); History.Destroy(); }
            void ClearCounts() { Points.Count = 0; SpriteInstances.Count = 0; RibbonInstances.Count = 0; History.Particles = 0; }
        };

        // Render-side state of one spawner driven by a ParticleSystem asset. Lives on the heap behind a
        // TOwningPointer: the spawner map is rehashed and copied by value, and the particle columns
        // (tens of MB) must not move with it. Emitters are matched to the asset's by emitter UUID.
        struct RenderParticleSystem
        {
            UInt64 SystemUuid = 0;
            UInt32 Revision = 0;
            DynamicArray<TOwningPointer<ParticleEmitterInstance>> Emitters;
            DynamicArray<RenderParticleEmitterGPU> EmitterBuffers; // parallel to Emitters
            DynamicArray<float> PointScratch;                      // interleaved xyz, reused every frame
            DynamicArray<ParticleInstanceGPU> InstanceScratch;     // sprite instances, reused every frame
            DynamicArray<float> ParameterDefaults;                 // copied from the program on adoption
            DynamicArray<CompiledParameterSlot> ParameterLayout;   // same, for the Debug Particles panel only

            // Last values synced from the snapshot (see ParticleSpawnerRenderObject).
            UInt32 SyncedActivation = 0;
            UInt32 SyncedClear = 0;
            UInt64 SyncedRequested = 0;
            UInt32 CompletedActivation = 0;

            // This frame's inputs, filled by SyncParticleSpawners, consumed by TickParticleSpawners.
            EParticleEmissionState State = EParticleEmissionState::Stopped;
            Vec3 Location = Vec3(0.0f);
            Quaternion Rotation = Quaternion(1.0f, 0.0f, 0.0f, 0.0f);
            DynamicArray<float> ParameterValues;
            UInt32 PendingExtraSpawn = 0;

            // Spawned by a ParticleCommand (SceneWorld::SpawnParticleSystem), not by a component: no snapshot
            // lists it, so reconciliation leaves it alone. DetachedState stands in for the snapshot entry —
            // ProcessParticleCommands writes it, SyncParticleSpawners feeds it to SyncParticleSystemSpawner
            // every frame, and the tick destroys the system once its run has no work left.
            bool Detached = false;
            ParticleSpawnerRenderObject DetachedState;
            // Detached only: seconds of simulation left before the system deactivates itself; 0 = no limit.
            float RemainingLifetime = 0.0f;
        };

        // A simulated spawner plus the GL buffer its particles are drawn from: either the legacy
        // ParticleSpawner (System is null) or an asset-driven RenderParticleSystem (Spawner is null). The
        // buffer is a plain handle (copied on rehash) — DestroyRenderParticleSpawner frees both, exactly once.
        struct RenderParticleSpawner
        {
            TOwningPointer<ParticleSpawner> Spawner;
            ParticlePointBuffer PointBuffer;
            TOwningPointer<RenderParticleSystem> System;
        };
        HashMap<EngineObjectHandle, HashMap<UInt64, RenderParticleSpawner>> mParticleSpawners;
        void SyncParticleSystemSpawner(const ParticleSpawnerRenderObject& state,
                                       const CompiledParticleSystem& program,
                                       const DynamicArray<float>& snapshotParameterValues, RenderParticleSpawner& spawner);
        // Reconciles mParticleSpawners[snapshot->SceneHandle] with snapshot->ParticleSpawners. Detached
        // systems are never reconciled away; they are synced from their DetachedState instead.
        void SyncParticleSpawners(RenderSnapshot* snapshot);
        // Drains the main -> render ParticleCommand queue (RenderParticleCommands.h): creates, deactivates and
        // destroys detached systems in whichever world each command names.
        void ProcessParticleCommands();
        // Programs of the detached systems by system uuid, filled from the commands that carry one. Never
        // pruned — mirrors the sent-revision map on the main side, which assumes the cache keeps everything.
        HashMap<UInt64, CompiledParticleSystem> mDetachedParticlePrograms;
        void DestroyRenderParticleSpawner(RenderParticleSpawner& spawner);
        void DestroyParticleSpawners();
        // Ticks the spawners of snapshot's world and uploads their positions / sprite instances. Runs
        // before the shadow passes, which draw the same buffers. view projects the velocity-stretch
        // direction of sprite instances.
        void TickParticleSpawners(RenderSnapshot* snapshot, float deltaTime, const Matrix4& view);
        // Draws the spawners of snapshot's world as points (ParticlePointProgram) into the bound
        // main buffer. Not editor-only: particles are a gameplay effect.
        // Also darkens the spawners whose ParticleClass receives shadows, from the directional
        // cascades already bound for the main pass.
        void RenderParticles(RenderSnapshot* snapshot, const Matrix4& view, const Matrix4& viewProj);
        // Transparent pass of the asset-driven emitters: sprites (ParticleSpriteProgram, instanced quads)
        // and ribbons (ParticleRibbonProgram, triangle strips), all from the emitters' SSBOs. Depth test on,
        // depth writes off; Additive or AlphaBlend per emitter, emitters of both kinds sorted back to front
        // together (never per particle). Sets and RESTORES the blend function and depth mask — the editor
        // grid, debug lines and ImGui inherit them. Neither casts nor receives shadows, and both stay out of
        // the depth prepass.
        void RenderTransparentParticles(RenderSnapshot* snapshot, const Matrix4& view, const Matrix4& projection);
        // Empty VAO for the attribute-less sprite / ribbon draws (core profile requires one bound).
        unsigned int mParticleSpriteVao = 0;
        // Draw list of RenderTransparentParticles, reused every frame.
        // One renderer of one emitter: an emitter with both a ribbon and sprites gives two entries.
        struct ParticleSpriteDraw
        {
            const RenderParticleEmitterGPU* Buffers = nullptr;
            const ParticleRenderParams* Params = nullptr;
            float DistanceSq = 0.0f;
            // Tie-break within one emitter (same centroid): the ribbon (0) under its sprites (1).
            UInt8 Layer = 0;
        };
        DynamicArray<ParticleSpriteDraw> mParticleSpriteDraws;
        // Draws the shadow-casting spawners of snapshot's world into the bound depth target (one
        // cascade or spot slot) with mParticleShadowShader. projection and resolution size each
        // particle's shadow square to ParticleClass::ShadowSize metres. The caller owns the GL
        // state: GL_PROGRAM_POINT_SIZE, and a scissor when the target is a region of an atlas —
        // wide points are not clipped to the viewport.
        void DrawParticleShadowCasters(RenderSnapshot* snapshot, const Matrix4& viewProj,
                                       const Matrix4& projection, Int32 resolution);
        // Debug Particles panel: copies the state of every spawner (see RenderParticleStats.h).
        [[nodiscard]] ParticleDebugStats GatherParticleDebugStats(RenderSnapshot* snapshot, float deltaTime) const;
        // One asset-driven spawner's part of it: lifecycle, parameter block, per-emitter rows. Walks the live
        // particles (bounds), so it runs only on request like the rest of the gather.
        [[nodiscard]] ParticleSystemSpawnerDebugStats GatherParticleSystemDebugStats(UInt64 spawnerUuid, const RenderParticleSystem& system) const;
        // Seconds since the last Debug Particles publish (throttle, see RenderSnapshot).
        float mParticleDebugStatsTimer = 0.0f;
        EngineObjectHandle mLastFrameSceneHandle;
    public:
        Renderer() = default;
        ~Renderer() = default;

        TUsePointer<FrameBuffer> GetMainFrameBuffer();

        // Debug cascade viewer (see mShadowLayerViewRequest). Call RequestShadowCascadeView every
        // frame the viewer is open; the copy lands one frame later and survives a resolution
        // change, because the destination texture is rebuilt alongside the array.
        void RequestShadowCascadeView(Int32 layer);
        TUsePointer<Texture> GetShadowCascadeView();
        [[nodiscard]] Int32 GetShadowCascadeLayerCount() const { return static_cast<Int32>(mCascadeAtlasRects.Size()); }

        // Same viewer, for a slot of the spot shadow atlas.
        void RequestSpotShadowView(Int32 slot);
        TUsePointer<Texture> GetSpotShadowView();
        [[nodiscard]] Int32 GetSpotShadowSlotCount() const { return mSpotShadowSlotCount; }

        void Initialize(ApplicationInfo* applicationInfo);
        void RenderSnapshot(RenderSnapshot* snapshot, float deltaTime);
        void Shutdown();
    };
}

#endif //PLUENGINE_RENDERER_H
