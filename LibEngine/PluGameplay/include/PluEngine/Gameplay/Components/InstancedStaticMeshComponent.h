//
// Created by Plutex on 7/19/26.
//

#ifndef PLUENGINE_INSTANCEDSTATICMESHCOMPONENT_H
#define PLUENGINE_INSTANCEDSTATICMESHCOMPONENT_H
#include "PluEngine/Gameplay/WorldComponent.h"
#include "InstancedStaticMeshComponent.generated.h"
#include "PluEngine/Render/RenderingInterfaces.h"
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
		Vec3 Rotation = Vec3(0.0f); // Euler degrees, jak WorldComponent.
		PLU_PROPERTY()
		Vec3 Scale = Vec3(1.0f);
	};

	// Dziedziczy bezpośrednio z WorldComponent, NIE z StaticMeshComponent: PhysicsWorld testuje
	// IsDerivedOfOrSame(StaticMeshComponent) w trzech miejscach i zbudowałby jedno ciało kolizji
	// z transformu komponentu, ignorując wszystkie instancje. Osobna baza usuwa problem bez
	// dotykania fizyki, kosztem powtórzenia StaticMeshToDisplay/Material/CastsShadow/MeshBoundingBox.
	PLU_CLASS(PyExport)
	class PLUGAMEPLAY_API InstancedStaticMeshComponent : public WorldComponent
	{
		REFLECTION_BODY_INSTANCEDSTATICMESHCOMPONENT()
	private:
		// Cache macierzy świata per instancja, przebudowywany leniwie w GetInstanceWorldMatrices()
		// gdy Instances się zmieniło (mInstanceCacheDirty LUB zawartość różni się od mCachedInstances)
		// albo world matrix komponentu jest inny niż przy ostatniej przebudowie. WorldComponent nie ma
		// wirtualnego hooka na zmianę transformu (MarkWorldMatrixForRegeneration jest prywatne),
		// więc porównujemy GetWorldMatrix() wprost zamiast dodawać taki hook.
		//
		// mInstanceCacheDirty sam w sobie NIE wystarczy: łapie tylko mutacje przez AddInstance/
		// RemoveInstance/UpdateInstance/ClearInstances/SetInstances. Edycja z panelu detali (reflekcja
		// - ArrayTreeEditorControl w TypeTraits.cpp) oraz deserializacja sceny piszą prosto po bajtach
		// `Instances` przez PropertyInfo::GetPtr, z pominięciem tych setterów - bez porównania zawartości
		// takie edycje wyglądałyby jak "ignorowane" (wartość w panelu się zmienia, ale zrenderowane
		// instancje stoją w miejscu, bo cache nigdy się nie przebudowuje). MeshInstanceTransform jest
		// POD (3x Vec3, brak paddingu), więc memcmp całej tablicy jest tani i bezpieczny.
		DynamicArray<Matrix4> mCachedWorldMatrices;
		// Równoległa do mCachedWorldMatrices: transpose(inverse(world)) per instancja, liczona
		// w tej samej przebudowie cache'u — inverse() 4x4 per instancja per klatka jest za drogie
		// w RenderSnapshotBuilderze przy dużej liczbie instancji.
		DynamicArray<Matrix4> mCachedNormalMatrices;
		DynamicArray<MeshInstanceTransform> mCachedInstances;
		// Transform version of the component at the last cache rebuild (0 = never built). Replaces
		// comparing the whole world matrix byte by byte on every access.
		UInt32 mCachedTransformVersion = 0;
		bool mInstanceCacheDirty = true;
	public:
		// World-space bounding sphere of one instance. Cached alongside the matrices because the
		// render snapshot builder needs one per instance per frame, and deriving it there cost three
		// glm::length plus a matrix-vector product per instance — for a spawner with a million
		// instances that is the whole frame.
		struct InstanceBoundingSphere
		{
			Vec3  Center = Vec3(0.0f);
			float Radius = 0.0f;
		};
	private:
		DynamicArray<InstanceBoundingSphere> mCachedInstanceBounds;
	public:
		InstancedStaticMeshComponent() = default;
		~InstancedStaticMeshComponent() override = default;

		PLU_PROPERTY(Setter=SetStaticMesh, Getter=GetStaticMesh)
		TUsePointer<StaticMesh> StaticMeshToDisplay;

		PLU_PROPERTY()
		TUsePointer<MaterialInfo> Material;

		PLU_PROPERTY(PyExport)
		bool CastsShadow = true;

		// Model-space bounds of the displayed mesh. Assign through SetMeshBoundingBox, never
		// directly — the cached per-instance spheres are derived from it.
		BoundingBox MeshBoundingBox;
		// Twardy guard, ten sam kontrakt co StaticMeshComponent::MeshBoundingBoxComputed:
		// CreateBoundingBoxForStaticMesh chodzi po każdym wierzchołku, więc liczymy raz.
		bool MeshBoundingBoxComputed = false;

		// Transformy instancji względem komponentu (lokalne Location/Rotation/Scale).
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
		// Swap-with-last (jak Unreal HISM) - O(1), ale NIE zachowuje stabilności indeksów: po
		// usunięciu, instancja spod ostatniego indeksu ląduje pod zwolnionym `index`.
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

		// Bulk, tylko C++: nie eksportować do Pythona dopóki nie zweryfikowano, że generator
		// bindingów radzi sobie z DynamicArray<PLU_STRUCT> (patrz plan instancingu).
		void SetInstances(const DynamicArray<MeshInstanceTransform>& instances);

		// Macierze świata per instancja (world matrix komponentu * lokalny transform instancji).
		// Jedyny producent mCachedWorldMatrices - przebudowuje cache gdy jest brudny.
		const DynamicArray<Matrix4>* GetInstanceWorldMatrices();
		// Macierze normalnych per instancja, równoległe (ten sam indeks) do GetInstanceWorldMatrices().
		// Wewnętrznie odświeża cache przez GetInstanceWorldMatrices().
		const DynamicArray<Matrix4>* GetInstanceNormalMatrices();
		// World bounding spheres per instance, parallel (same index) to GetInstanceWorldMatrices().
		// Built in the same rebuild pass, from MeshBoundingBox and the instance's world matrix.
		const DynamicArray<InstanceBoundingSphere>* GetInstanceWorldBounds();

		// Sets the model-space box and invalidates the instance cache (the bounding spheres above are
		// derived from it). The only supported way to change MeshBoundingBox.
		void SetMeshBoundingBox(const BoundingBox& boundingBox);
	};
}

#endif //PLUENGINE_INSTANCEDSTATICMESHCOMPONENT_H
