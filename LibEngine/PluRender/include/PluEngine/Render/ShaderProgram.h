//
// Created by Plutex on 1/19/26.
//

#ifndef PLUENGINE_SHADERPROGRAM_H
#define PLUENGINE_SHADERPROGRAM_H
#include "glad.h"
#include "PluEngine/Core/Objects/EngineObject.h"
#include "ShaderProgram.generated.h"
// ShaderProgramInfo moved to the asset types; kept included so consumers of this header are
// unaffected (PluRender sits above PluAssetTypes, so this is a downward include).
#include "PluEngine/AssetTypes/ShaderProgramInfo.h"
#include <algorithm>
#include <atomic>
#include <string>
#include <sstream>

#include "PluEngine/PluTypes.h"
#include "PluBase/PluUUID.h"
#include "PluEngine/Core/IAssetData.h"

namespace Plu
{
	struct MaterialInfo;

	inline String Sanitize(const String& s)
	{
		std::string out = s.CStr();
		std::replace(out.begin(), out.end(), ' ', '_');
		std::replace(out.begin(), out.end(), '/', '_');
		return out.c_str();
	}

	inline String BuildShaderCacheName()
	{
		//const char* vendor   = reinterpret_cast<const char*>(glGetString(GL_VENDOR));
		//const char* renderer = reinterpret_cast<const char*>(glGetString(GL_RENDERER));
		const char* version  = reinterpret_cast<const char*>(glGetString(GL_VERSION));

#ifdef PLU_PLATFORM_WINDOWS
		String system = "Windows";
#elif defined(PLU_PLATFORM_LINUX)
		String system = "Linux";
#elif defined(PLU_PLATFORM_MACOS)
		String system = "macOS";
#else
		String system = "UnknownOS";
#endif

		std::stringstream ss;
		ss << system.CStr() << "_"
		   //<< Sanitize(vendor).CStr() << "_"
		   //<< Sanitize(renderer).CStr() << "_"
		   << "Driver" << Sanitize(version).CStr();

		return ss.str().c_str();
	}

	PLU_STRUCT()
	class IShaderCode;
	PLU_CLASS()
	class PLURENDER_API ShaderProgram : public EngineObject
	{
		REFLECTION_BODY_SHADERPROGRAM()
	private:
		UInt16 mProgramID = 0;

		//Elements
		TUsePointer<IShaderCode> mVertexShader;
		TUsePointer<IShaderCode> mFragmentShader;

		HashMap<String, int> mUniformLocationCache;

		// Liczba jednostek teksturujących zarezerwowanych przez silnik (np. mapy cieni kaskad)
		// zanim materiał zacznie bindować swoje tekstury. RenderFromMaterial startuje od tej wartości.
		int mSlotsUsed = 0;

		// Hot reload: main-thread ustawia flagę gdy źródło shadera się zmieniło, a wątek renderu
		// (gdzie żyje kontekst GL) konsumuje ją i wykonuje faktyczny Recompile(). Wywołania GL
		// nie mogą lecieć z wątku main.
		std::atomic<bool> mRecompileRequested{false};

		// Cache zapytania o blok SSBO "BoneMatrices" (vertex skinning); -1 = jeszcze nie
		// sprawdzone dla aktualnie zlinkowanego programu. Resetowane przy każdej zmianie mProgramID.
		int mHasBoneMatricesBlock = -1;

		// Cache zapytania o blok SSBO "InstanceMatrices" (instancing); -1 = jeszcze nie sprawdzone
		// dla aktualnie zlinkowanego programu. Resetowane przy każdej zmianie mProgramID (patrz
		// UnloadProgram) — inaczej hot-reload zostaje ze starą odpowiedzią.
		int mHasInstanceDataBlock = -1;

		// To samo dla bloku "VisibleInstanceIndices" — shader adresujący instancje przez indirekcję
		// widocznych indeksów obsługuje WSZYSTKIE passy (patrz BasicVertInstanced.vert). Shader
		// instancingowy bez tego bloku to starsza konwencja: rysuje się go jak dotąd, a pass głębi
		// zostaje przy silnikowym shaderze.
		int mHasVisibleIndexBlock = -1;

		// Jeden lookup w mUniformLocationCache (Find zamiast Contains + operator[] ×2);
		// na miss pyta GL i cache'uje wynik (także -1 — nieobecne uniformy nie pytają GL co klatkę).
		int GetUniformLocation(const String& name);

		void SaveBinary() const;
	public:
		ShaderProgram();
		~ShaderProgram() override;

		[[nodiscard]] bool HasNecessarySubshaders() const;

		PluUUID Uuid;

		void SetVertexShader(TUsePointer<IShaderCode> vertexShader);
		void SetFragmentShader(TUsePointer<IShaderCode> fragmentShader);

		TUsePointer<IShaderCode> GetVertexShader();
		TUsePointer<IShaderCode> GetFragmentShader();

		[[nodiscard]] bool IsLoaded() const
		{ return mProgramID != 0; }

		void RenderFromMaterial(MaterialInfo* materialInfo, TUsePointer<class RenderingManager> renderingManager);

		// Ustawia ile jednostek teksturujących zajmuje silnik (cienie) zanim zacznie materiał.
		void SetSlotsUsed(int slots) { mSlotsUsed = slots; }
		[[nodiscard]] int GetSlotsUsed() const { return mSlotsUsed; }

		//Setters — no-op (bez Bind i bez wywołania GL), gdy uniform nie istnieje w programie
		//(lokacja -1). Ustawianie uniformów globalnych na wszystkich programach pozostaje tanie.
		void SetMatrix4Uniform(const String& name, const Matrix4& matrix);
		void SetVec2Uniform(const String& name, const Vec2& vec);
		void SetVec3Uniform(const String& name, const Vec3& vec);
		void SetVec4Uniform(const String& name, const Vec4& vec);
		void SetIntUniform(const String& name, int value);
		void SetFloatUniform(const String& name, float value);
		void SetBoolUniform(const String& name, bool value);
		void SetTextureUniform(const String& name, TUsePointer<class Texture> texture, int textureUnit);

		void Bind() const;

		// glUseProgram jest deduplikowany cache'em aktualnie zbindowanego programu (render thread
		// only, jak wszystkie wywołania GL tutaj). Wołać na początku klatki oraz po każdym miejscu,
		// gdzie program mógł zostać zbindowany/skasowany poza ShaderProgram::Bind (np. backend ImGui).
		static void ResetBindCache();

		bool Recompile(); //Just straight up recompile the shader, no checks
		void UnloadProgram();

		// Zgłasza chęć rekompilacji z dowolnego wątku (main). Faktyczny GL Recompile robi
		// wątek renderu przez ConsumeRecompileRequest() -> Recompile().
		void RequestRecompile() { mRecompileRequested.store(true, std::memory_order_release); }
		bool ConsumeRecompileRequest() { return mRecompileRequested.exchange(false, std::memory_order_acq_rel); }

		[[nodiscard]] bool BinaryExists() const;
		void LoadFromBinary(); //Tries to load from binary, if fails then Recompiles

		// Czy zlinkowany program deklaruje blok SSBO "BoneMatrices" (skinning skeletal meshy).
		// Wołać z wątku renderu po IsLoaded(); wynik GL query jest cache'owany per link.
		[[nodiscard]] bool HasBoneMatricesBlock();

		// Czy zlinkowany program deklaruje blok SSBO "InstanceMatrices" (instancing static meshy).
		// Wołać z wątku renderu po IsLoaded(); wynik GL query jest cache'owany per link.
		[[nodiscard]] bool HasInstanceDataBlock();

		// Czy zlinkowany program adresuje instancje przez blok "VisibleInstanceIndices" (konwencja
		// wspólna dla passa głównego, prepassu i map cieni — patrz BasicVertInstanced.vert).
		// Wołać z wątku renderu po IsLoaded(); wynik GL query jest cache'owany per link.
		[[nodiscard]] bool HasVisibleIndexBlock();
	};
}

#endif //PLUENGINE_SHADERPROGRAM_H
