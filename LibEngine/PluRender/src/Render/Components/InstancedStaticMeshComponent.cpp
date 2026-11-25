//
// Created by Plutex on 7/19/26.
//

#include "PluEngine/Render/Components/InstancedStaticMeshComponent.h"
#include "PluEngine/AssetTypes/MeshBounds.h"

#include <cstring>

#include "PluEngine/Gameplay/GameObject.h"
#include "glm/gtc/matrix_inverse.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "PluEngine/AssetTypes/Material/Material.h"

Plu::TUsePointer<Plu::StaticMesh> Plu::InstancedStaticMeshComponent::GetStaticMesh()
{
	return StaticMeshToDisplay;
}

void Plu::InstancedStaticMeshComponent::SetStaticMesh(TUsePointer<StaticMesh> staticMesh)
{
	StaticMeshToDisplay = staticMesh;
	mInstanceCacheDirty = true;
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
