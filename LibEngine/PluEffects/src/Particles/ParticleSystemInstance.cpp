//
// Created by Plutex on 9/27/26.
//

#include "PluEngine/Effects/Particles/ParticleSystemInstance.h"
#include "PluEngine/Effects/Particles/ParticleSystem.h"

namespace Plu
{
#ifdef PLU_ENGINE_EDITOR_BUILD
	namespace
	{
		void RemoveLiveInstance(UInt64 systemUuid, ParticleSystemInstance* instance)
		{
			if (systemUuid == 0) return;
			DynamicArray<ParticleSystemInstance*>& list = ParticleSystemInstance::GetLiveInstances(systemUuid);
			for (UInt64 i = 0; i < list.Size(); ++i) {
				if (list[i] == instance) { list.RemoveAt(i); break; }
			}
		}
	}

	ParticleSystemInstance::~ParticleSystemInstance()
	{
		RemoveLiveInstance(mBoundSystemUuid, this);
	}

	DynamicArray<ParticleSystemInstance*>& ParticleSystemInstance::GetLiveInstances(UInt64 systemUuid)
	{
		static HashMap<UInt64, DynamicArray<ParticleSystemInstance*>> sLiveInstances;
		return sLiveInstances[systemUuid];
	}
#endif

	void ParticleSystemInstance::BindTo(const TUsePointer<ParticleSystem>& system)
	{
		if (!system) {
#ifdef PLU_ENGINE_EDITOR_BUILD
			RemoveLiveInstance(mBoundSystemUuid, this);
#endif
			mSystem = nullptr;
			mBoundSystemUuid = 0;
			mBoundParametersRevision = 0;
			return;
		}

		const UInt64 newUuid = system->Uuid.getUUID();
		const bool sameAsset = mSystem && mBoundSystemUuid == newUuid;

		if (sameAsset && mBoundParametersRevision == system->ParametersRevision) {
			mSystem = system;
			return;
		}

		if (sameAsset) {
			mParameters.MergeFrom(system->Parameters); // layout edited (e.g. in PIE): keep surviving values
		} else {
#ifdef PLU_ENGINE_EDITOR_BUILD
			RemoveLiveInstance(mBoundSystemUuid, this);
#endif
			mParameters.RebuildFrom(system->Parameters);
#ifdef PLU_ENGINE_EDITOR_BUILD
			GetLiveInstances(newUuid).PushBack(this);
#endif
		}

		mSystem = system;
		mBoundSystemUuid = newUuid;
		mBoundParametersRevision = system->ParametersRevision;
	}

	bool ParticleSystemInstance::SetFloat(const String& name, float value) { return mParameters.Set<float>(name, value); }
	float ParticleSystemInstance::GetFloat(const String& name, float fallback) { float v = fallback; mParameters.TryGet<float>(name, v); return v; }
	bool ParticleSystemInstance::SetInt(const String& name, int value) { return mParameters.Set<int>(name, value); }
	int ParticleSystemInstance::GetInt(const String& name, int fallback) { int v = fallback; mParameters.TryGet<int>(name, v); return v; }
	bool ParticleSystemInstance::SetBool(const String& name, bool value) { return mParameters.Set<bool>(name, value); }
	bool ParticleSystemInstance::GetBool(const String& name, bool fallback) { bool v = fallback; mParameters.TryGet<bool>(name, v); return v; }
	bool ParticleSystemInstance::SetVec3(const String& name, Vec3 value) { return mParameters.Set<Vec3>(name, value); }
	Vec3 ParticleSystemInstance::GetVec3(const String& name, Vec3 fallback) { Vec3 v = fallback; mParameters.TryGet<Vec3>(name, v); return v; }
	bool ParticleSystemInstance::SetColor(const String& name, Vec4 value) { return mParameters.Set<Vec4>(name, value); }
	Vec4 ParticleSystemInstance::GetColor(const String& name, Vec4 fallback) { Vec4 v = fallback; mParameters.TryGet<Vec4>(name, v); return v; }
	bool ParticleSystemInstance::HasParameter(const String& name) { return mParameters.Find(name).IsValid(); }

	DynamicArray<String> ParticleSystemInstance::GetParameterNames()
	{
		DynamicArray<String> names;
		for (const TOwningPointer<IParticleParameter>& parameter : mParameters.GetAll())
			if (parameter) names.PushBack(parameter->Name);
		return names;
	}
}
