//
// Created by Plutex on 1/19/26.
//

#ifndef PLUENGINE_STATICMESHCOMPONENT_H
#define PLUENGINE_STATICMESHCOMPONENT_H
#include "PluEngine/Gameplay/WorldComponent.h"
#include "StaticMeshComponent.generated.h"
#include "PluEngine/Render/RenderingInterfaces.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"

namespace Plu
{
	struct StaticMesh;
	PLU_CLASS(PyExport)
	class PLUGAMEPLAY_API StaticMeshComponent : public WorldComponent
	{
		REFLECTION_BODY_STATICMESHCOMPONENT()
	public:
		StaticMeshComponent() = default;
		~StaticMeshComponent() override = default;

		PLU_PROPERTY(Setter=SetStaticMesh, Getter=GetStaticMesh)
		TUsePointer<StaticMesh> StaticMeshToDisplay;

		PLU_PROPERTY()
		TUsePointer<MaterialInfo> Material;

		// Model-space bounds of the displayed mesh. Assign through SetMeshBoundingBox, never
		// directly — the cached world sphere below would keep the previous box.
		BoundingBox MeshBoundingBox;
		// Twardy guard: CreateBoundingBoxForStaticMesh chodzi po każdym wierzchołku, więc liczymy
		// raz (przy SetStaticMesh, jeśli mesh już załadowany, albo leniwie w RenderSnapshotBuilder,
		// gdy dojedzie asynchronicznie), nigdy per klatka.
		bool MeshBoundingBoxComputed = false;

		PLU_PROPERTY(PyExport)
		bool CastsShadow = true;

		PLU_FUNCTION(PyExport)
		TUsePointer<StaticMesh> GetStaticMesh();
		PLU_FUNCTION(PyExport)
		void SetStaticMesh(TUsePointer<StaticMesh> staticMesh);

		PLU_FUNCTION(PyExport)
		TUsePointer<MaterialInfo> GetMaterial();
		PLU_FUNCTION(PyExport)
		void SetMaterial(TUsePointer<MaterialInfo> material);

		//Rendering
		Matrix4 GetRenderMatrix();

		// World-space bounding sphere of this component's mesh: MeshBoundingBox put through the
		// component's world transform. Cached against GetTransformVersion() and the local box, so a
		// component that did not move answers from memory — RenderSnapshotBuilder asks every static
		// mesh component in the scene for this on every frame.
		void GetWorldBoundingSphere(Vec3& outCenter, float& outRadius);

		// Sets the model-space box and drops the cached world sphere. The only supported way to
		// change MeshBoundingBox.
		void SetMeshBoundingBox(const BoundingBox& boundingBox);

	private:
		// World sphere derived from MeshBoundingBox and the world matrix. Valid while
		// mWorldBoundsVersion equals the component's transform version; 0 means "never computed"
		// and is also what SetMeshBoundingBox resets it to (versions start at 1).
		Vec3 mWorldBoundsCenter = Vec3(0.0f);
		float mWorldBoundsRadius = 0.0f;
		UInt32 mWorldBoundsVersion = 0;

	protected:
		// Only meaningful for meshes that carry collision shapes — those are baked into the owning
		// object's compound shape at this component's relative transform.
		void OnRelativeTransformChanged() override;
	};
}

#endif //PLUENGINE_STATICMESHCOMPONENT_H
