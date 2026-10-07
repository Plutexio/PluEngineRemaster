//
// Created by Plutex on 7/6/26.
//

#ifndef PLUENGINE_SKELETALMESHCOMPONENT_H
#define PLUENGINE_SKELETALMESHCOMPONENT_H
#include "PluEngine/Gameplay/WorldComponent.h"
#include "SkeletalMeshComponent.generated.h"
#include "PluEngine/Render/RenderingInterfaces.h"
#include "PluEngine/AssetTypes/SkeletalMesh/SkeletalMesh.h"
#include "PluEngine/AssetTypes/Animation/BoneTransform.h"
#include "PluEngine/AssetTypes/Animation/AnimGraphInstance.h"
// AnimationToShow is a reflected TUsePointer<Animation>, and the generated TypeSerializer needs
// the complete type — a forward declaration only compiles while some other translation unit in
// the same unity batch happens to supply the definition.
#include "PluEngine/AssetTypes/Animation/SkeletalAnimation.h"
#include "HashMap/HashMap.h"
#include "Pointers/Casts.h"
#include <utility>

namespace Plu
{
	struct SkeletonAttachPoint;
	struct Animation;
	struct SkeletalMesh;
	struct AnimationGraph;

	// Space a posed node transform is reported in (SkeletalMeshComponent::GetNodeLocation/Rotation).
	PLU_ENUM(PyNamespace=Plu, PyExport)
	enum class NodeSpace
	{
		WORLD,     // world space: the component's live world matrix * the node's posed global
		LOCAL,     // relative to the parent node — the node's posed local transform
		COMPONENT  // skeleton space (root-relative): unaffected by where the component is
	};

	PLU_CLASS(PyExport, PyDerive)
	class PLUGAMEPLAY_API SkeletalMeshComponent : public WorldComponent
	{
		REFLECTION_BODY_SKELETALMESHCOMPONENT()
	private:
		DynamicArray<TOwningPointer<SkeletonNode>> mNodes;
		// Edge detector so playback (re)starts from the scrubbed AnimationFrameToShow.
		bool mWasPlaying = false;

		// Attach point name -> node index in the skeleton's SkeletonPoseLayout, resolved on first
		// use so repeated attach point queries cost no name lookup. Cleared with the mesh.
		HashMap<String, Int32> mAttachPointNodeCache;

		// Per-component live AnimGraph values — this component's own "user" of AnimGraph, never
		// shared with any other component even when they point at the same graph asset. Not a
		// PLU_PROPERTY: runtime state, never serialized, never touched by scene duplication (a
		// duplicated actor just gets a fresh null and builds its own instance on first use).
		TOwningPointer<AnimGraphInstance> mAnimGraphInstance;
	public:
		SkeletalMeshComponent() = default;
		~SkeletalMeshComponent() override = default;

		PLU_PROPERTY(Setter=SetSkeletalMesh, Getter=GetSkeletalMesh)
		TUsePointer<SkeletalMesh> SkeletalMeshToDisplay;

		PLU_PROPERTY()
		TUsePointer<MaterialInfo> Material;

		// Raw (graph-less) animation: sampled directly with no blending/state-machine logic.
		// Ignored while AnimGraph is assigned — the graph takes priority — but never cleared, so
		// unassigning AnimGraph falls straight back to this path.
		PLU_PROPERTY()
		TUsePointer<Animation> AnimationToShow;

		PLU_PROPERTY()
		int AnimationFrameToShow = 0;

		// Drives the pose instead of AnimationToShow when assigned (checked first by
		// RenderSnapshotBuilder). Null by default — components that never touch the graph system
		// pay nothing extra and behave exactly as before.
		PLU_PROPERTY()
		TUsePointer<AnimationGraph> AnimGraph;

		PLU_PROPERTY(PyExport)
		bool IsPlaying = false;

		PLU_PROPERTY(PyExport)
		bool LoopAnimation = true;

		// Playback head in animation ticks; runtime-only (advanced by OnUpdate during play,
		// read by RenderSnapshotBuilder the same frame — main thread both ways).
		float AnimationTimeTicks = 0.0f;

		// Playback head in seconds for AnimGraph evaluation (AnimEvalContext::TimeSeconds is
		// seconds, not ticks — each AnimSampleNode converts using its own animation's FPS).
		// Separate from AnimationTimeTicks because the graph has no single FPS of its own.
		// Runtime-only, advanced by OnUpdate while IsPlaying and AnimGraph is assigned.
		float GraphTimeSeconds = 0.0f;

		// Per-bone temporary pose overrides, keyed by node name. Each is a parent-space delta
		// pre-multiplied onto the bind/animated local matrix (localMatrix = override * base), so it
		// can translate, rotate and scale a bone and drag its subtree. Applied identically by
		// RenderSnapshotBuilder and the editor bone overlay. Deliberately NOT a PLU_PROPERTY: this
		// is a live posing scratchpad that is never serialized. Empty in normal play (zero overhead).
		HashMap<String, Matrix4> BoneLocalOverrides;

		// Cache palety kości (pary OffsetMatrix / global transform) z ostatniego builda snapshotu.
		// Poza jest funkcją (mesh, animacja, tick, overrides, world matrix — patrz CachedPoseWorldMatrix
		// niżej) — więc gdy klucz się nie zmienił, RenderSnapshotBuilder pomija cały traversal szkieletu
		// (mapy stringowe, Tracks.Find i dynamic_cast per węzeł) i reużywa tej tablicy. Aktywne
		// BoneLocalOverrides wyłączają cache (CachedPoseValid zostaje false), więc live posing zawsze
		// przelicza. Kod mutujący Animation w miejscu (te same UUID i tick, inne klucze) musi zrzucić
		// CachedPoseValid ręcznie. Producent: RenderSnapshotBuilder::BuildSnapshotAndPublish.
		DynamicArray<std::pair<Matrix4, Matrix4>> CachedBonePalette;

		// Skeleton-space (root-relative) pose per node, indexed by SkeletonPoseLayout node index —
		// i.e. every node, not just bones (CachedBonePalette holds the bones-only, offset-multiplied
		// matrix form the skinning shader wants). Kept as BoneTransform so nothing on the per-frame
		// path builds a matrix; the attach point queries convert the single node they need.
		// Empty until the first pose evaluation; survives a pose cache hit, since a skipped
		// evaluation means the values are still current.
		// Producent: RenderSnapshotBuilder::BuildSnapshotAndPublish.
		Pose PosedGlobalTransforms;
		UInt64 CachedPoseMeshUuid = 0;
		UInt64 CachedPoseAnimUuid = 0;
		double CachedPoseTicks = -1.0;
		bool CachedPoseValid = false;
		// AnimGraphInstance::GetVariables().GetValueRevision() at the last pose build. Needed on top
		// of the (mesh, anim, ticks) key above: a SetFloat() while the graph's time is stopped
		// changes nothing else in that key, so without this the cache would keep serving the stale
		// pose forever.
		UInt32 CachedPoseGraphValueRevision = 0;

		// Component's world matrix at the last pose build. World-space graph nodes (AnimTransformBoneNode)
		// fold the component transform into the local-space pose itself, so unlike everything else in
		// this cache key the pose is no longer independent of ModelMatrix — an object that moved while
		// the graph's time is stopped still needs its pose rebuilt. Costs 16 float comparisons per
		// component per frame and a cache miss on every move even without a world-space node; scanning
		// the graph for "does it use World?" would need its own cached, invalidated-on-edit flag for a
		// case that barely matters (a moving object is animating almost always anyway).
		Matrix4 CachedPoseWorldMatrix = Matrix4(0.0f);

		void OnUpdate(float deltaTime) override;

		// Runs once per frame immediately before this component's AnimGraph is evaluated, and only
		// while a graph is assigned and compatible with the mesh — the engine's equivalent of an
		// UE AnimBlueprint EventGraph. Pull everything the graph needs into GetAnimGraphInstance()
		// here (IK goals, speeds, state flags) instead of pushing it from some object's OnUpdate:
		// SceneWorld::TickScene walks a hash map, so its tick order is arbitrary and a pushed value
		// can be a frame stale, while everything read here is from the current frame by construction.
		// Main thread, called by RenderSnapshotBuilder after every GameObject has ticked.
		PLU_FUNCTION(PyOverride)
		virtual void OnPreEvaluateAnimGraph(float deltaTime) {}

		// Bind-pose bounds of the assigned mesh, in local space. Walking every vertex is expensive,
		// so it is computed once — in SetSkeletalMesh when the mesh is already loaded, otherwise
		// lazily by RenderSnapshotBuilder once the async load lands. MeshBoundingBoxComputed is the
		// hard guard (mirrors StaticMeshComponent). Used to cull shadow casters per cascade;
		// animation moves vertices outside these bounds, so the renderer inflates the radius.
		BoundingBox MeshBoundingBox;
		bool MeshBoundingBoxComputed = false;

		PLU_PROPERTY(PyExport)
		bool CastsShadow = true;

		PLU_FUNCTION(PyExport)
		TUsePointer<SkeletalMesh> GetSkeletalMesh();
		PLU_FUNCTION(PyExport)
		void SetSkeletalMesh(TUsePointer<SkeletalMesh> skeletalMesh);

		PLU_FUNCTION(PyExport)
		TUsePointer<MaterialInfo> GetMaterial();
		PLU_FUNCTION(PyExport)
		void SetMaterial(TUsePointer<MaterialInfo> material);

		// AnimGraph itself is a plain PLU_PROPERTY (no PyExport, no Setter/Getter override — plain
		// assignment needs no side effects, EnsureAnimGraphInstance's BindTo already detects a changed
		// graph on its own). These are the Python-facing accessors, same pattern as Get/SetMaterial.
		PLU_FUNCTION(PyExport)
		TUsePointer<AnimationGraph> GetAnimGraph();
		PLU_FUNCTION(PyExport)
		void SetAnimGraph(TUsePointer<AnimationGraph> animGraph);

		// Creates this component's AnimGraphInstance on first use and (re)binds it to the current
		// AnimGraph — cheap when nothing changed (see AnimGraphInstance::BindTo). Null when no
		// AnimGraph is assigned.
		AnimGraphInstance* EnsureAnimGraphInstance();
		// Python-facing alias — `comp.GetAnimGraphInstance().SetFloat("Speed", 5.0)`.
		PLU_FUNCTION(PyExport)
		AnimGraphInstance* GetAnimGraphInstance();

		//Rendering
		Matrix4 GetRenderMatrix();
		DynamicArray<TOwningPointer<SkeletonNode>>* GetNodes();

		//AttachPoints
		// Full world frame of an attach point: componentWorld * parentNodeGlobal * attachPointLocal.
		// False when the mesh, the attach point, its parent node or a posed transform for that node
		// is missing (i.e. before the first snapshot build). Prefer this over the location/rotation
		// pair below when the whole frame is wanted — it keeps the bone's basis exactly.
		bool TryGetAttachPointWorldMatrix(const String& attachPointName, Matrix4& outMatrix);

		PLU_FUNCTION(PyExport)
		Vec3 GetAttachPointLocationInWorld(String attachPointName);
		PLU_FUNCTION(PyExport)
		Vec3 GetAttachPointRotationInWorld(String attachPointName);

		// Posed transform of skeleton node `boneName` (any node, not just bones; not an attach point)
		// in `space`. Read from the LAST pose build (PosedGlobalTransforms), so inside OnUpdate it is
		// one frame old — consistent with attach points. Zero when the mesh, the node or a pose is
		// missing (before the first snapshot build). Rotation in euler degrees.
		PLU_FUNCTION(PyExport)
		Vec3 GetNodeLocation(String boneName, NodeSpace space);
		PLU_FUNCTION(PyExport)
		Vec3 GetNodeRotation(String boneName, NodeSpace space);

		// Shared resolve for GetNodeLocation/Rotation. False when the mesh, the node or a posed
		// transform for it is missing.
		bool TryGetNodeTransform(const String& nodeName, NodeSpace space, BoneTransform& outTransform);

		// Re-expresses a world-space location in the posed frame of skeleton node `nodeName`.
		// Deliberately built on the component's world matrix FROM THE LAST POSE BUILD
		// (CachedPoseWorldMatrix), not the live one: inside OnPreEvaluateAnimGraph the posed
		// transforms are still last frame's, and so is everything derived from them — attach
		// point queries (on this component or any other) and the transforms of objects riding
		// attach points. Converting such a value with the matching-epoch world matrix makes the
		// staleness cancel, so for anything moving rigidly with `nodeName` the result equals the
		// CURRENT offset — exactly what a Bone-space graph goal wants (see EIKGoalSpace::Bone).
		// Returns the input unchanged when the mesh/node is missing or no pose was built yet.
		PLU_FUNCTION(PyExport)
		Vec3 WorldLocationToNodeSpace(String nodeName, Vec3 worldLocation);
		// Rotation variant of the above; euler degrees both ways (engine-wide convention).
		PLU_FUNCTION(PyExport)
		Vec3 WorldRotationToNodeSpace(String nodeName, Vec3 worldRotationDegrees);

	private:
		// Shared resolve for the WorldXToNodeSpace pair: the node's posed world frame at the
		// last pose build's epoch (CachedPoseWorldMatrix * PosedGlobalTransforms[node]). False
		// when the mesh or node is missing, or no pose has been built yet.
		bool TryGetNodeWorldMatrixAtLastPose(const String& nodeName, Matrix4& outMatrix);
	public:

	};
}

#endif //PLUENGINE_SKELETALMESHCOMPONENT_H
