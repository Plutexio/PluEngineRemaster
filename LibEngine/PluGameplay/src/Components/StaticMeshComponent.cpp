//
// Created by Plutex on 1/19/26.
//

#include "PluEngine/Gameplay/Components/StaticMeshComponent.h"
#include "PluEngine/AssetTypes/MeshBounds.h"

#include "PluEngine/Gameplay/GameObject.h"

Plu::TUsePointer<Plu::StaticMesh> Plu::StaticMeshComponent::GetStaticMesh()
{
	return StaticMeshToDisplay;
}

void Plu::StaticMeshComponent::SetStaticMesh(TUsePointer<StaticMesh> staticMesh)
{
	StaticMeshToDisplay = staticMesh;
	MeshBoundingBoxComputed = false;
	mWorldBoundsVersion = 0;
	if (staticMesh && staticMesh->IsLoaded) {
		SetMeshBoundingBox(Plu::CreateBoundingBoxForStaticMesh(staticMesh.GetRaw()));
	}
	DispatchEvent("StaticMeshChanged", nullptr);
}

void Plu::StaticMeshComponent::OnRelativeTransformChanged()
{
}

Plu::TUsePointer<Plu::MaterialInfo> Plu::StaticMeshComponent::GetMaterial()
{
	return Material;
}

void Plu::StaticMeshComponent::SetMaterial(TUsePointer<MaterialInfo> material)
{
	Material = material;
}

Matrix4 Plu::StaticMeshComponent::GetRenderMatrix()
{
	return GetWorldMatrix();
}

void Plu::StaticMeshComponent::GetWorldBoundingSphere(Vec3 &outCenter, float &outRadius)
{
	const UInt32 transformVersion = GetTransformVersion();
	if (mWorldBoundsVersion != transformVersion) {
		const Matrix4& worldMatrix = GetWorldMatrixRef();
		mWorldBoundsCenter = Vec3(worldMatrix * Vec4(MeshBoundingBox.GetCenter(), 1.0f));
		// Sphere, not a box: invariant under rotation, so only the scale of the extent matters.
		mWorldBoundsRadius = glm::length(MeshBoundingBox.GetExtent() * glm::abs(GetWorldScale()));
		mWorldBoundsVersion = transformVersion;
	}
	outCenter = mWorldBoundsCenter;
	outRadius = mWorldBoundsRadius;
}

void Plu::StaticMeshComponent::SetMeshBoundingBox(const BoundingBox &boundingBox)
{
	MeshBoundingBox = boundingBox;
	MeshBoundingBoxComputed = true;
	mWorldBoundsVersion = 0;
}
