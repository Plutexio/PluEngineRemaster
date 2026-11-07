//
// Created by Plutex on 1/4/26.
//

#ifndef PLUENGINE_ASSETSMANAGER_H
#define PLUENGINE_ASSETSMANAGER_H
#include "PluEngine/Core/Objects/EngineObject.h"
#include "IAssetData.generated.h"
#include "PluBase/PluUUID.h"

namespace Plu
{
	class PluUUID;

	PLU_STRUCT(UUID=Uuid, PyExport)
	struct PLUCORE_API IAssetData
	{
		REFLECTION_BODY_IASSETDATA()

		// Asset data is owned as TOwningPointer<IAssetData> and deleted through this base —
		// without a virtual destructor the derived destructors (TextureInfo pixels, SceneInfo
		// containers, ...) never run.
		virtual ~IAssetData() = default;

		PLU_PROPERTY()
		PluUUID Uuid;
	};
}

#endif //PLUENGINE_ASSETSMANAGER_H
