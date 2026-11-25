//
// Created by Plutex on 7/19/26.
//

#ifndef PLUENGINE_INSTANCEDSTATICMESHCOMPONENT_H
#define PLUENGINE_INSTANCEDSTATICMESHCOMPONENT_H
#include "PluEngine/Gameplay/WorldComponent.h"
#include "InstancedStaticMeshComponent.generated.h"
#include "PluEngine/AssetTypes/Material/Material.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"

namespace Plu
{
	PLU_STRUCT()
	struct PLUGAMEPLAY_API MeshInstanceTransform
	{
		REFLECTION_BODY_MESHINSTANCETRANSFORM()

		PLU_PROPERTY()
		Vec3 Location = Vec3(0.0f);
		PLU_PROPERTY()
		Vec3 Rotation = Vec3(0.0f);
		PLU_PROPERTY()
		Vec3 Scale = Vec3(1.0f);
	};

	//TODO this class's just a mock up
	PLU_CLASS(PyExport)
	class PLUGAMEPLAY_API InstancedStaticMeshComponent : public WorldComponent
	{
		REFLECTION_BODY_INSTANCEDSTATICMESHCOMPONENT()
	private:
		bool mInstanceCacheDirty = true;
	public:
		InstancedStaticMeshComponent() = default;
		~InstancedStaticMeshComponent() override = default;

		PLU_PROPERTY(Setter=SetStaticMesh, Getter=GetStaticMesh)
		TUsePointer<StaticMesh> StaticMeshToDisplay;

		PLU_PROPERTY()
		TUsePointer<MaterialInfo> Material;

		PLU_PROPERTY(PyExport)
		bool CastsShadow = true;

		PLU_PROPERTY()
		DynamicArray<MeshInstanceTransform> Instances;

		PLU_FUNCTION(PyExport)
		TUsePointer<StaticMesh> GetStaticMesh();
		PLU_FUNCTION(PyExport)
		void SetStaticMesh(TUsePointer<StaticMesh> staticMesh);

		PLU_FUNCTION(PyExport)
		TUsePointer<MaterialInfo> GetMaterial();
		PLU_FUNCTION(PyExport)
		void SetMaterial(TUsePointer<MaterialInfo> material);

		PLU_FUNCTION(PyExport)
		Int32 AddInstance(Vec3 loc, Vec3 rot, Vec3 scale);
		PLU_FUNCTION(PyExport)
		bool RemoveInstance(Int32 index);
		PLU_FUNCTION(PyExport)
		bool UpdateInstance(Int32 index, Vec3 loc, Vec3 rot, Vec3 scale);
		PLU_FUNCTION(PyExport)
		void ClearInstances();
		PLU_FUNCTION(PyExport)
		Int32 GetInstanceCount() const;
		PLU_FUNCTION(PyExport)
		void ReserveInstances(Int32 count);

		void SetInstances(const DynamicArray<MeshInstanceTransform>& instances);
	};
}

#endif //PLUENGINE_INSTANCEDSTATICMESHCOMPONENT_H
