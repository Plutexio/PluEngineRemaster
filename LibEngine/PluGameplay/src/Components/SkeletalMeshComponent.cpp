//
// Created by Plutex on 7/6/26.
//

#include "PluEngine/Gameplay/Components/SkeletalMeshComponent.h"
#include "PluEngine/AssetTypes/MeshBounds.h"

#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "PluEngine/PluUtils.h"
#include "PluEngine/AssetTypes/Animation/SkeletalAnimation.h"
#include "PluEngine/AssetTypes/Skeleton/Skeleton.h"
#include "PluEngine/Gameplay/GameObject.h"
#include "PluEngine/Core/BoundingBox.h"

void Plu::SkeletalMeshComponent::OnUpdate(float deltaTime)
{
	if (!IsPlaying)
	{
		mWasPlaying = false;
		return;
	}

	// Graph time runs independently of the raw-animation tick head below — the graph has no
	// single FPS of its own, and a component may have both assigned (graph wins at render time,
	// see RenderSnapshotBuilder) or just one.
	if (AnimGraph)
	{
		GraphTimeSeconds += deltaTime;
		if (AnimGraphInstance* instance = EnsureAnimGraphInstance()) {
			instance->TimeSeconds = GraphTimeSeconds;
		}
	}

	if (!AnimationToShow)
	{
		mWasPlaying = false;
		return;
	}

	if (!mWasPlaying)
	{
		// Play just started (or resumed) — pick up from the scrubbed frame.
		AnimationTimeTicks = static_cast<float>(AnimationFrameToShow);
		mWasPlaying = true;
	}

	AnimationTimeTicks += deltaTime * AnimationToShow->FramesPerSecond;

	const float duration = static_cast<float>(AnimationToShow->FramesAmount);
	if (AnimationTimeTicks > duration)
	{
		if (LoopAnimation && duration > 0.0f)
		{
			AnimationTimeTicks = std::fmod(AnimationTimeTicks, duration);
		}
		else
		{
			AnimationTimeTicks = duration;
			IsPlaying = false;
		}
	}

	// Mirror into the scrub property so pausing freezes on the current pose.
	AnimationFrameToShow = static_cast<int>(AnimationTimeTicks);
}

Plu::TUsePointer<Plu::SkeletalMesh> Plu::SkeletalMeshComponent::GetSkeletalMesh()
{
	return SkeletalMeshToDisplay;
}

void Plu::SkeletalMeshComponent::SetSkeletalMesh(TUsePointer<SkeletalMesh> skeletalMesh)
{
	mNodes.Clear();
	SkeletalMeshToDisplay = skeletalMesh;
	MeshBoundingBoxComputed = false;
	if (SkeletalMeshToDisplay) {
		SkeletalMeshToDisplay->MeshSkeleton->CreateNodePalette(&mNodes);
		// Only when the mesh is already loaded — otherwise RenderSnapshotBuilder catches up once
		// the async load lands (same pattern as StaticMeshComponent).
		if (SkeletalMeshToDisplay->IsLoaded) {
			MeshBoundingBox = Plu::CreateBoundingBoxForSkeletalMesh(SkeletalMeshToDisplay.GetRaw());
			MeshBoundingBoxComputed = true;
		}
	}
	mAttachPointNodeCache.Clear();
	PosedGlobalTransforms.Clear();
	CachedPoseValid = false;
}

Plu::TUsePointer<Plu::MaterialInfo> Plu::SkeletalMeshComponent::GetMaterial()
{
	return Material;
}

void Plu::SkeletalMeshComponent::SetMaterial(TUsePointer<MaterialInfo> material)
{
	Material = material;
}

Plu::TUsePointer<Plu::AnimationGraph> Plu::SkeletalMeshComponent::GetAnimGraph()
{
	return AnimGraph;
}

void Plu::SkeletalMeshComponent::SetAnimGraph(TUsePointer<AnimationGraph> animGraph)
{
	AnimGraph = animGraph;
}

Matrix4 Plu::SkeletalMeshComponent::GetRenderMatrix()
{
	return GetWorldMatrix();
}

DynamicArray<Plu::TOwningPointer<Plu::SkeletonNode>> * Plu::SkeletalMeshComponent::GetNodes()
{
	return &mNodes;
}

bool Plu::SkeletalMeshComponent::TryGetAttachPointWorldMatrix(const String& attachPointName, Matrix4& outMatrix)
{
	if (!SkeletalMeshToDisplay || !SkeletalMeshToDisplay->MeshSkeleton) {
		return false;
	}
	TOwningPointer<SkeletonAttachPoint>* attachPointFind = SkeletalMeshToDisplay->MeshSkeleton->AttachPoints.Find(attachPointName);
	if (attachPointFind == nullptr) {
		PLU_CORE_ERROR("No attach point named {0}", attachPointName.CStr());
		return false;
	}
	TUsePointer<SkeletonAttachPoint> attachPoint = *attachPointFind;

	// Resolve the parent node to a pose-layout index once per attach point, then reuse it.
	Int32 parentIndex;
	if (const Int32* cached = mAttachPointNodeCache.Find(attachPointName)) {
		parentIndex = *cached;
	} else {
		parentIndex = SkeletalMeshToDisplay->MeshSkeleton->GetPoseLayout().FindIndex(attachPoint->ParentNodeName);
		if (parentIndex < 0) {
			PLU_CORE_ERROR("No Node with name {0}", attachPoint->ParentNodeName.CStr());
			return false;
		}
		mAttachPointNodeCache.Insert(attachPointName, parentIndex);
	}

	// Out of range means the pose has not been evaluated yet (no snapshot built since the mesh
	// was assigned), so there is no posed transform to hand out.
	if (static_cast<UInt64>(parentIndex) >= PosedGlobalTransforms.Size()) {
		return false;
	}

	// PosedGlobalTransforms is skeleton-space (root-relative), so the component's own world
	// matrix has to go in front of it, and the attach point's offset has to ride the posed bone's
	// frame instead of being added along world axes.
	outMatrix = GetWorldMatrix()
		* PosedGlobalTransforms[static_cast<UInt64>(parentIndex)].ToMatrix()
		* attachPoint->GetLocalMatrix();
	return true;
}

Vec3 Plu::SkeletalMeshComponent::GetAttachPointLocationInWorld(String attachPointName)
{
	Matrix4 attachPointWorld;
	if (!TryGetAttachPointWorldMatrix(attachPointName, attachPointWorld)) {
		return this->GetWorldLocation();
	}
	return GetLocationFromMatrix(attachPointWorld);
}

Vec3 Plu::SkeletalMeshComponent::GetAttachPointRotationInWorld(String attachPointName)
{
	Matrix4 attachPointWorld;
	if (!TryGetAttachPointWorldMatrix(attachPointName, attachPointWorld)) {
		return this->GetWorldRotation();
	}
	return GetRotationFromMatrix(attachPointWorld);
}

bool Plu::SkeletalMeshComponent::TryGetNodeTransform(const String& nodeName, NodeSpace space, BoneTransform& outTransform)
{
	if (!SkeletalMeshToDisplay || !SkeletalMeshToDisplay->MeshSkeleton) {
		return false;
	}
	const SkeletonPoseLayout& layout = SkeletalMeshToDisplay->MeshSkeleton->GetPoseLayout();
	const Int32 nodeIndex = layout.FindIndex(nodeName);
	if (nodeIndex < 0) {
		PLU_CORE_ERROR("No Node with name {0}", nodeName.CStr());
		return false;
	}
	// Out of range means no pose has been evaluated yet (same as TryGetAttachPointWorldMatrix).
	if (static_cast<UInt64>(nodeIndex) >= PosedGlobalTransforms.Size()) {
		return false;
	}

	// PosedGlobalTransforms is skeleton space already — the COMPONENT answer as is.
	const BoneTransform& global = PosedGlobalTransforms[static_cast<UInt64>(nodeIndex)];
	switch (space) {
		case NodeSpace::COMPONENT:
			outTransform = global;
			return true;
		case NodeSpace::LOCAL:
		{
			const Int32 parentIndex = layout.ParentIndex[static_cast<UInt64>(nodeIndex)];
			outTransform = parentIndex < 0 ? global
				: PosedGlobalTransforms[static_cast<UInt64>(parentIndex)].Inverse().Compose(global);
			return true;
		}
		case NodeSpace::WORLD:
			// Live component matrix over the last pose — the same pairing attach points use.
			outTransform = BoneTransform::FromMatrix(GetWorldMatrix() * global.ToMatrix());
			return true;
	}
	return false;
}

Vec3 Plu::SkeletalMeshComponent::GetNodeLocation(String boneName, NodeSpace space)
{
	BoneTransform transform;
	if (!TryGetNodeTransform(boneName, space, transform)) {
		return {0.0f, 0.0f, 0.0f};
	}
	return transform.Location;
}

Vec3 Plu::SkeletalMeshComponent::GetNodeRotation(String boneName, NodeSpace space)
{
	BoneTransform transform;
	if (!TryGetNodeTransform(boneName, space, transform)) {
		return {0.0f, 0.0f, 0.0f};
	}
	// Euler through glm, the inverse of the Quaternion(radians(euler)) every rotation setter uses.
	return glm::degrees(glm::eulerAngles(transform.Rotation));
}

bool Plu::SkeletalMeshComponent::TryGetNodeWorldMatrixAtLastPose(const String& nodeName, Matrix4& outMatrix)
{
	if (!SkeletalMeshToDisplay || !SkeletalMeshToDisplay->MeshSkeleton) {
		return false;
	}
	const Int32 nodeIndex = SkeletalMeshToDisplay->MeshSkeleton->GetPoseLayout().FindIndex(nodeName);
	if (nodeIndex < 0) {
		PLU_CORE_ERROR("No Node with name {0}", nodeName.CStr());
		return false;
	}
	// Out of range means no pose has been evaluated yet (same situation as in
	// TryGetAttachPointWorldMatrix) — and it also guarantees CachedPoseWorldMatrix was never
	// written, so bailing here keeps the zero-initialized matrix from ever being used.
	if (static_cast<UInt64>(nodeIndex) >= PosedGlobalTransforms.Size()) {
		return false;
	}
	outMatrix = CachedPoseWorldMatrix * PosedGlobalTransforms[static_cast<UInt64>(nodeIndex)].ToMatrix();
	return true;
}

Vec3 Plu::SkeletalMeshComponent::WorldLocationToNodeSpace(String nodeName, Vec3 worldLocation)
{
	Matrix4 nodeWorld;
	if (!TryGetNodeWorldMatrixAtLastPose(nodeName, nodeWorld)) {
		return worldLocation;
	}
	return Vec3(glm::inverse(nodeWorld) * Vec4(worldLocation, 1.0f));
}

Vec3 Plu::SkeletalMeshComponent::WorldRotationToNodeSpace(String nodeName, Vec3 worldRotationDegrees)
{
	Matrix4 nodeWorld;
	if (!TryGetNodeWorldMatrixAtLastPose(nodeName, nodeWorld)) {
		return worldRotationDegrees;
	}
	// FromMatrix strips the scale off the basis before quat_cast, so this stays correct for a
	// scaled component/skeleton.
	const Quaternion nodeRotation = BoneTransform::FromMatrix(nodeWorld).Rotation;
	const Quaternion localRotation = glm::conjugate(nodeRotation) * Quaternion(glm::radians(worldRotationDegrees));
	return glm::degrees(glm::eulerAngles(localRotation));
}

Plu::AnimGraphInstance* Plu::SkeletalMeshComponent::EnsureAnimGraphInstance()
{
	if (!AnimGraph) return nullptr;
	if (!mAnimGraphInstance) {
		mAnimGraphInstance = CreateOwning<AnimGraphInstance>();
#ifdef PLU_ENGINE_EDITOR_BUILD
		TUsePointer<GameObject> owner = GetParentGameObject();
		mAnimGraphInstance->DebugName = (owner ? owner->GetObjectName() : String("?"))
			+ " / " + GetClass()->TypeName;
#endif
	}
	mAnimGraphInstance->BindTo(AnimGraph);
	return mAnimGraphInstance.GetRaw();
}

Plu::AnimGraphInstance* Plu::SkeletalMeshComponent::GetAnimGraphInstance()
{
	return EnsureAnimGraphInstance();
}

