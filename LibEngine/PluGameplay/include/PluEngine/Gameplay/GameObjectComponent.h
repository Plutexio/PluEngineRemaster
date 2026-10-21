//
// Created by Plutex on 1/18/26.
//

#ifndef PLUENGINE_GAMEOBJECTCOMPONENT_H
#define PLUENGINE_GAMEOBJECTCOMPONENT_H
#include "GameObjectComponent.generated.h"
#include "PluBase/PluUUID.h"
#include "PluEngine/Core/Objects/EngineObject.h"

namespace Plu
{
	class GameObject;
	class SceneWorld;
	PLU_CLASS(PyExport, PyDerive)
	class PLUGAMEPLAY_API GameObjectComponent : public EngineObject
	{
		REFLECTION_BODY_GAMEOBJECTCOMPONENT()
	private:
		TUsePointer<GameObject> mParentObject;

		friend class GameObject;
		friend class SceneWorld;
		void SetParentGameObject(TUsePointer<GameObject> newParent);

		bool mIsActivated = true;
		PLU_PROPERTY()
		String mComponentName;
	protected:
		TUsePointer<EngineObjectManager> GetObjectManagerFromParent();
		TUsePointer<SceneWorld> GetWorld();
		TUsePointer<GameObjectComponent> This();
		TOwningPointer<GameObjectComponent> ThisAsOwner();
	public:
		GameObjectComponent() = default;
		virtual ~GameObjectComponent() override = default;

		PLU_FUNCTION(PyOverride)
		virtual void OnSetupComponent() {}

		PLU_FUNCTION(PyOverride)
		virtual void OnBeginPlay() {}

		PLU_FUNCTION(PyOverride)
		virtual void OnUpdate(float deltaTime) {}

		PLU_FUNCTION(PyOverride)
		virtual void OnEndPlay() {}

		PLU_FUNCTION()
		[[nodiscard]] bool IsActivated() const;

		PLU_FUNCTION()
		void Activate();
		PLU_FUNCTION()
		void Deactivate();

		PLU_FUNCTION()
		[[nodiscard]] TUsePointer<GameObject> GetParentGameObject() const;

		PLU_PROPERTY()
		PluUUID Uuid;

		PLU_FUNCTION()
		String GetComponentName();

		void SetComponentName(String name);
	};
}

#endif //PLUENGINE_GAMEOBJECTCOMPONENT_H
