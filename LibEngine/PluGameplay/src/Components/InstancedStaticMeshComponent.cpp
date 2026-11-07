//
// Created by Plutex on 7/19/26.
//

#include "PluEngine/Gameplay/Components/InstancedStaticMeshComponent.h"
#include "PluEngine/AssetTypes/MeshBounds.h"

#include <cstring>

#include "PluEngine/Gameplay/GameObject.h"
#include "glm/gtc/matrix_inverse.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"

Plu::TUsePointer<Plu::StaticMesh> Plu::InstancedStaticMeshComponent::GetStaticMesh()
{
	return StaticMeshToDisplay;
}

void Plu::InstancedStaticMeshComponent::SetStaticMesh(TUsePointer<StaticMesh> staticMesh)
{
	StaticMeshToDisplay = staticMesh;
	MeshBoundingBoxComputed = false;
	mInstanceCacheDirty = true;
	if (staticMesh && staticMesh->IsLoaded) {
		SetMeshBoundingBox(Plu::CreateBoundingBoxForStaticMesh(staticMesh.GetRaw()));
	}
}

Plu::TUsePointer<Plu::MaterialInfo> Plu::InstancedStaticMeshComponent::GetMaterial()
{
	return Material;
}

void Plu::InstancedStaticMeshComponent::SetMaterial(TUsePointer<MaterialInfo> material)
{
	Material = material;
}

Int32 Plu::InstancedStaticMeshComponent::AddInstance(Vec3 loc, Vec3 rot, Vec3 scale)
{
	MeshInstanceTransform& inst = Instances.EmplaceBack();
	inst.Location = loc;
	inst.Rotation = rot;
	inst.Scale = scale;
	mInstanceCacheDirty = true;
	return static_cast<Int32>(Instances.Size() - 1);
}

bool Plu::InstancedStaticMeshComponent::RemoveInstance(Int32 index)
{
	if (index < 0 || static_cast<UInt32>(index) >= Instances.Size()) return false;
	const UInt32 lastIndex = Instances.Size() - 1;
	if (static_cast<UInt32>(index) != lastIndex) {
		Instances[index] = Instances[lastIndex];
	}
	Instances.RemoveAt(lastIndex);
	mInstanceCacheDirty = true;
	return true;
}

bool Plu::InstancedStaticMeshComponent::UpdateInstance(Int32 index, Vec3 loc, Vec3 rot, Vec3 scale)
{
	if (index < 0 || static_cast<UInt32>(index) >= Instances.Size()) return false;
	MeshInstanceTransform& inst = Instances[index];
	inst.Location = loc;
	inst.Rotation = rot;
	inst.Scale = scale;
	mInstanceCacheDirty = true;
	return true;
}

void Plu::InstancedStaticMeshComponent::ClearInstances()
{
	Instances.Clear();
	mInstanceCacheDirty = true;
}

Int32 Plu::InstancedStaticMeshComponent::GetInstanceCount() const
{
	return static_cast<Int32>(Instances.Size());
}

void Plu::InstancedStaticMeshComponent::ReserveInstances(Int32 count)
{
	Instances.Reserve(static_cast<UInt32>(count));
}

void Plu::InstancedStaticMeshComponent::SetInstances(const DynamicArray<MeshInstanceTransform>& instances)
{
	Instances = instances;
	mInstanceCacheDirty = true;
}

const DynamicArray<Matrix4>* Plu::InstancedStaticMeshComponent::GetInstanceWorldMatrices()
{
	const Matrix4& componentWorld = GetWorldMatrixRef();
	const UInt32 transformVersion = GetTransformVersion();

	// Rozmiar/zawartość porównane wprost: mInstanceCacheDirty łapie tylko AddInstance/RemoveInstance/
	// UpdateInstance/ClearInstances/SetInstances. Edycje z panelu detali (reflekcja) i deserializacja
	// sceny piszą po bajtach Instances z pominięciem tych metod, więc bez tego porównania cache
	// nigdy by się nie odświeżył po takiej edycji - w rezultacie liczba w panelu zmienia się, ale
	// zrenderowana instancja stoi w miejscu (wygląda jak "zmiana zignorowana").
	const bool instancesContentChanged = Instances.Size() != mCachedInstances.Size() ||
		(!Instances.IsEmpty() && std::memcmp(Instances.Data(), mCachedInstances.Data(), Instances.Size() * sizeof(MeshInstanceTransform)) != 0);

	if (mInstanceCacheDirty || instancesContentChanged || mCachedTransformVersion != transformVersion) {
		mCachedWorldMatrices.Clear();
		mCachedWorldMatrices.Reserve(Instances.Size());
		mCachedNormalMatrices.Clear();
		mCachedNormalMatrices.Reserve(Instances.Size());
		mCachedInstanceBounds.Clear();
		mCachedInstanceBounds.Reserve(Instances.Size());
		const Vec3 localCenter = MeshBoundingBox.GetCenter();
		const Vec3 localExtent = MeshBoundingBox.GetExtent();
		for (const MeshInstanceTransform& inst : Instances) {
			const Matrix4 local = glm::translate(glm::mat4(1.0f), inst.Location) *
				glm::mat4_cast(glm::quat(glm::radians(inst.Rotation))) *
				glm::scale(glm::mat4(1.0f), inst.Scale);
			const Matrix4 world = componentWorld * local;
			mCachedWorldMatrices.PushBack(world);
			// Only mat3(normalMatrix) is ever read by the shaders, and for an affine matrix that
			// block is the inverse-transpose of the matrix's own 3x3 — same result, no 4x4 inverse.
			mCachedNormalMatrices.PushBack(Matrix4(glm::inverseTranspose(glm::mat3(world))));

			// Scale per instance is not stored separately (only the final matrix), so it is read
			// back off the basis column lengths — the equivalent of GetWorldScale() for a plain
			// component, correct as long as there is no shear (translate * rotate * scale).
			const Vec3 instanceScale = Vec3(glm::length(Vec3(world[0])),
											glm::length(Vec3(world[1])),
											glm::length(Vec3(world[2])));
			InstanceBoundingSphere& sphere = mCachedInstanceBounds.EmplaceBack();
			sphere.Center = Vec3(world * Vec4(localCenter, 1.0f));
			sphere.Radius = glm::length(localExtent * instanceScale);
		}
		mCachedTransformVersion = transformVersion;
		mCachedInstances = Instances;
		mInstanceCacheDirty = false;
	}
	return &mCachedWorldMatrices;
}

const DynamicArray<Matrix4>* Plu::InstancedStaticMeshComponent::GetInstanceNormalMatrices()
{
	GetInstanceWorldMatrices();
	return &mCachedNormalMatrices;
}

const DynamicArray<Plu::InstancedStaticMeshComponent::InstanceBoundingSphere>*
Plu::InstancedStaticMeshComponent::GetInstanceWorldBounds()
{
	GetInstanceWorldMatrices();
	return &mCachedInstanceBounds;
}

void Plu::InstancedStaticMeshComponent::SetMeshBoundingBox(const BoundingBox &boundingBox)
{
	MeshBoundingBox = boundingBox;
	MeshBoundingBoxComputed = true;
	mInstanceCacheDirty = true;
}
