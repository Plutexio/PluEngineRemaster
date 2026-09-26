//
// Created by Plutex on 9/27/26.
//

#ifndef PLUENGINE_PARTICLESYSTEMINSTANCE_H
#define PLUENGINE_PARTICLESYSTEMINSTANCE_H

#include "PluEngine/Core.h"
#include "PluEngine/Effects/Particles/ParticleParameterStore.h"
#include "Pointers/TUsePointer.h"
#include "ParticleSystemInstance.generated.h"

namespace Plu
{
	struct ParticleSystem;

	// Per-component runtime state of a ParticleSystem asset: the live parameter values (asset defaults
	// plus whatever gameplay set). MAIN THREAD. The values reach the render thread as a flat float block
	// packed by RenderSnapshotBuilder; the render thread never sees this object.
	//
	// Python: `comp.GetSystemInstance().SetVec3("TracerColor", Vec3(1, 0, 0))`.
	PLU_STRUCT(PyExport)
	struct PLUEFFECTS_API ParticleSystemInstance
	{
		REFLECTION_BODY_PARTICLESYSTEMINSTANCE()

#ifdef PLU_ENGINE_EDITOR_BUILD
		~ParticleSystemInstance();
#endif

		// Cheap no-op while bound to the same asset at the same ParametersRevision. A layout edit
		// re-merges (keeping live values of surviving parameters); another asset rebuilds from defaults.
		void BindTo(const TUsePointer<ParticleSystem>& system);
		[[nodiscard]] TUsePointer<ParticleSystem> GetSystem() const { return mSystem; }
		ParticleParameterStore& GetParameters() { return mParameters; }

		PLU_FUNCTION(PyExport)
		bool SetFloat(const String& name, float value);
		PLU_FUNCTION(PyExport)
		float GetFloat(const String& name, float fallback = 0.0f);
		PLU_FUNCTION(PyExport)
		bool SetInt(const String& name, int value);
		PLU_FUNCTION(PyExport)
		int GetInt(const String& name, int fallback = 0);
		PLU_FUNCTION(PyExport)
		bool SetBool(const String& name, bool value);
		PLU_FUNCTION(PyExport)
		bool GetBool(const String& name, bool fallback = false);
		PLU_FUNCTION(PyExport)
		bool SetVec3(const String& name, Vec3 value);
		PLU_FUNCTION(PyExport)
		Vec3 GetVec3(const String& name, Vec3 fallback = Vec3(0.0f));
		// Colour parameter (RGBA).
		PLU_FUNCTION(PyExport)
		bool SetColor(const String& name, Vec4 value);
		PLU_FUNCTION(PyExport)
		Vec4 GetColor(const String& name, Vec4 fallback = Vec4(1.0f));
		PLU_FUNCTION(PyExport)
		bool HasParameter(const String& name);
		PLU_FUNCTION(PyExport)
		DynamicArray<String> GetParameterNames();

#ifdef PLU_ENGINE_EDITOR_BUILD
		String DebugName;
		// Instances bound to `systemUuid` (PIE panel). Editor-only bookkeeping; not thread-safe.
		static DynamicArray<ParticleSystemInstance*>& GetLiveInstances(UInt64 systemUuid);
#endif

	private:
		TUsePointer<ParticleSystem> mSystem;
		ParticleParameterStore mParameters;
		UInt64 mBoundSystemUuid = 0;
		UInt32 mBoundParametersRevision = 0;
	};
}

#endif //PLUENGINE_PARTICLESYSTEMINSTANCE_H
