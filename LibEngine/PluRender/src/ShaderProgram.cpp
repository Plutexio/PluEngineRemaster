//
// Created by Plutex on 1/19/26.
//

#include "glad/glad.h"

#include "PluEngine/Render/ShaderProgram.h"
#include "glm/gtc/type_ptr.hpp"

#include "PluEngine/PluPaths.h"
#include "PluEngine/AssetTypes/Material/Material.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Render/RenderingManager.h"
#include "PluEngine/Render/GLTexture.h"
#include "PluEngine/Render/ShaderCacheWriter.h"
#include "PluEngine/AssetTypes/ShaderCode.h"

void Plu::ShaderProgram::SaveBinary() const
{
	PLU_CORE_ASSERT(mProgramID != 0, "Tried to save uncompiled Shader!")
	GLint binaryLength = 0;
	glGetProgramiv(mProgramID, GL_PROGRAM_BINARY_LENGTH, &binaryLength);

	std::vector<GLubyte> binary(binaryLength);
	GLenum binaryFormat = 0;

	glGetProgramBinary(mProgramID, binaryLength, nullptr, &binaryFormat, binary.data());;

	PathW outPath = GetGlobalShaderCacheWriter()->GetShaderCacheDirectory();
	outPath += L"/" + BuildShaderCacheName().ToWide() + L"/";
	outPath += StringW::FromInt(Uuid.getUUID()) + PLU_BINARY_EXT_W;

	std::filesystem::create_directory(outPath.GetParentPath().CStr());

	BinaryFileWriter writer(outPath.CStr());
	writer.Write(&binaryFormat, sizeof(binaryFormat));
	writer.Write(binary.data(), static_cast<long long>(binary.size()));
	writer.CloseFile();
	PLU_CORE_INFO("Saved binary shader to {}", outPath.ToString().ToNarrow().CStr());
}

Plu::ShaderProgram::ShaderProgram()
{
	mProgramID = 0;
}

Plu::ShaderProgram::~ShaderProgram()
{
}

bool Plu::ShaderProgram::HasNecessarySubshaders() const
{
	return mFragmentShader && mVertexShader;
}

void Plu::ShaderProgram::SetVertexShader(TUsePointer<IShaderCode> vertexShader)
{
	mVertexShader = vertexShader;
}

void Plu::ShaderProgram::SetFragmentShader(TUsePointer<IShaderCode> fragmentShader)
{
	mFragmentShader = fragmentShader;
}

Plu::TUsePointer<Plu::IShaderCode> Plu::ShaderProgram::GetVertexShader()
{
	return mVertexShader;
}

Plu::TUsePointer<Plu::IShaderCode> Plu::ShaderProgram::GetFragmentShader()
{
	return mFragmentShader;
}

namespace
{
	// Uniformy, które ustawia silnik (raz na klatkę dla globalnych, per draw dla reszty) i których
	// materiał nie ma prawa nadpisać. Lustro listy engineOnlyUniforms z PythonTools/
	// ShaderCodeParser.py — parser nie wypisuje ich do parametrów NOWEGO materiału, ale materiały
	// zapisane wcześniej wiozą je dalej w swoim JSON-ie, a wtedy RenderFromMaterial nadpisywał
	// wartość silnikową martwą wartością z assetu. Najdroższy przypadek: `time` = 0 w materiale
	// zatrzymywał animację wierzchołków (wiatr) na fazie zero, a `instanceBaseIndex` = 0 rysowałby
	// cały batch instancji z transformem pierwszej.
	bool IsEngineManagedUniform(const Plu::String& name)
	{
		return name == "model" || name == "normalMatrix" || name == "view" || name == "projection"
			|| name == "cameraPos" || name == "time" || name == "dirLightColor" || name == "dirLightDir"
			|| name == "shadowCascades" || name == "instanceBaseIndex" || name == "paletteBaseIndex";
	}
}

void Plu::ShaderProgram::RenderFromMaterial(MaterialInfo *materialInfo, TUsePointer<RenderingManager> renderingManager)
{
	// Startujemy od slotu zarezerwowanego przez silnik (np. mapy cieni kaskad zajmują 0..mSlotsUsed-1).
	int numOfTextures = mSlotsUsed;
	UInt32 numUniforms = materialInfo->MaterialParameters.Size();
	for (UInt32 i = 0; i < numUniforms; i++) {
		TUsePointer<IShaderUniform> uniform = materialInfo->MaterialParameters.At(i);
		if (!uniform) continue;
		if (uniform->ArraySize != 0) continue;
		if (IsEngineManagedUniform(uniform->Name)) {
			// Raz na materiał na sesję — wpis jest martwy, ale widać go w panelu materiału, więc
			// warto powiedzieć, co go zignorowało.
			static HashSet<UInt64> warnedMaterials;
			if (warnedMaterials.Insert(materialInfo->Uuid.getUUID())) {
				PLU_CORE_WARN("Material {} carries a parameter named '{}', which is set by the engine — ignoring it. "
							  "Re-save the material to drop the stale entry.",
							  materialInfo->Uuid.getUUID(), uniform->Name.CStr());
			}
			continue;
		}
		// Kolejność porównań wg częstości w typowych materiałach (PBR: sampler2D/bool/float/vec3)
		// — dispatch to łańcuch porównań Stringów robiony per uniform per batch per klatkę.
		if (uniform->Type == "sampler2D") {
			ShaderUniform<TUsePointer<TextureInfo>>* textureUniform = static_cast<ShaderUniform<TUsePointer<TextureInfo>>*>(uniform.GetRaw());
			TUsePointer<Texture> texture = renderingManager->GetTextureForInfo(textureUniform->Data);
			if (!texture) {
				renderingManager->RequestTextureFromInfo(textureUniform->Data);
			} else {
				SetTextureUniform(uniform->Name, texture, numOfTextures);
				numOfTextures++;
			}
		} else if (uniform->Type == "bool") {
			SetBoolUniform(uniform->Name, static_cast<ShaderUniform<bool>*>(uniform.GetRaw())->Data);
		} else if (uniform->Type == "float") {
			SetFloatUniform(uniform->Name, static_cast<ShaderUniform<float>*>(uniform.GetRaw())->Data);
		} else if (uniform->Type == "vec3") {
			SetVec3Uniform(uniform->Name, static_cast<ShaderUniform<Vec3>*>(uniform.GetRaw())->Data);
		} else if (uniform->Type == "vec2") {
			SetVec2Uniform(uniform->Name, static_cast<ShaderUniform<Vec2>*>(uniform.GetRaw())->Data);
		} else if (uniform->Type == "vec4") {
			SetVec4Uniform(uniform->Name, static_cast<ShaderUniform<Vec4>*>(uniform.GetRaw())->Data);
		} else if (uniform->Type == "int") {
			SetIntUniform(uniform->Name, static_cast<ShaderUniform<int>*>(uniform.GetRaw())->Data);
		}
	}
	numOfTextures = 0;
}

int Plu::ShaderProgram::GetUniformLocation(const String& name)
{
	if (const int* cached = mUniformLocationCache.Find(name)) {
		return *cached;
	}
	const int location = glGetUniformLocation(mProgramID, name.CStr());
	mUniformLocationCache.Insert(name, location);
	return location;
}

void Plu::ShaderProgram::SetMatrix4Uniform(const String& name, const Matrix4& matrix)
{
	const int location = GetUniformLocation(name);
	if (location < 0) return;
	Bind();
	glUniformMatrix4fv(location, 1, GL_FALSE, glm::value_ptr(matrix));
}

void Plu::ShaderProgram::SetVec2Uniform(const String& name, const Vec2& vec)
{
	const int location = GetUniformLocation(name);
	if (location < 0) return;
	Bind();
	glUniform2fv(location, 1, glm::value_ptr(vec));
}

void Plu::ShaderProgram::SetVec3Uniform(const String& name, const Vec3& vec)
{
	const int location = GetUniformLocation(name);
	if (location < 0) return;
	Bind();
	glUniform3fv(location, 1, glm::value_ptr(vec));
}

void Plu::ShaderProgram::SetVec4Uniform(const String& name, const Vec4& vec)
{
	const int location = GetUniformLocation(name);
	if (location < 0) return;
	Bind();
	glUniform4fv(location, 1, glm::value_ptr(vec));
}

void Plu::ShaderProgram::SetIntUniform(const String& name, int value)
{
	const int location = GetUniformLocation(name);
	if (location < 0) return;
	Bind();
	glUniform1i(location, value);
}

void Plu::ShaderProgram::SetFloatUniform(const String& name, float value)
{
	const int location = GetUniformLocation(name);
	if (location < 0) return;
	Bind();
	glUniform1f(location, value);
}

void Plu::ShaderProgram::SetBoolUniform(const String& name, bool value)
{
	const int location = GetUniformLocation(name);
	if (location < 0) return;
	Bind();
	glUniform1i(location, value);
}

void Plu::ShaderProgram::SetTextureUniform(const String& name, TUsePointer<class Texture> texture, int textureUnit)
{
	if (!texture) return;
	// Lokacja najpierw: gdy sampler nie istnieje w programie, nie bindujemy tekstury wcale
	// (dawniej tekstura lądowała na jednostce mimo braku uniformu — zbędny state change GL).
	const int location = GetUniformLocation(name);
	if (location < 0) return;
	Bind();
	texture->Bind(textureUnit);
	glUniform1i(location, textureUnit);
}

namespace
{
	// ID programu aktualnie zbindowanego przez ShaderProgram::Bind. Wszystkie wywołania GL lecą
	// z wątku renderu, więc zwykła zmienna wystarcza. Sentinel = "nieznany" (0 to ważny stan GL —
	// brak programu). Unieważniany na starcie klatki (Renderer::RenderSnapshot) — backend ImGui
	// binduje własny program surowym glUseProgram — oraz przy kasowaniu programu (GL może oddać
	// zwolnione ID nowemu programowi).
	constexpr UInt32 kUnknownProgram = 0xFFFFFFFFu;
	UInt32 gBoundProgramID = kUnknownProgram;
}

void Plu::ShaderProgram::ResetBindCache()
{
	gBoundProgramID = kUnknownProgram;
}

void Plu::ShaderProgram::Bind() const
{
	if (gBoundProgramID == mProgramID) return;
	glUseProgram(mProgramID);
	gBoundProgramID = mProgramID;
}

bool Plu::ShaderProgram::Recompile()
{
	if (!HasNecessarySubshaders()) {
		PLU_ERROR("Triggered Recompile on Unready Shader!");
		return false;
	}
	PLU_PROFILE_SCOPE_LOG("ShaderProgram recompilation");
	PLU_CORE_TRACE("Recompiling Shader {}", Uuid.getUUID());

	UInt16 vs = glCreateShader(GL_VERTEX_SHADER);
	String vsrc = mVertexShader->GetCode();
	const char* vsrc_c = vsrc.CStr();
	glShaderSource(vs, 1, &vsrc_c, nullptr);
	glCompileShader(vs);

	int success;
	char infoLog[512];
	glGetShaderiv(vs, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(vs, 512, nullptr, infoLog);
		PLU_CORE_ERROR("ERROR::SHADER::VERTEX::COMPILATION_FAILED\n{}", infoLog);
		return false;
	}

	GLuint fs = glCreateShader(GL_FRAGMENT_SHADER);
	String fsrc = mFragmentShader->GetCode();
	const char* fsrc_c = fsrc.CStr();
	glShaderSource(fs, 1, &fsrc_c, nullptr);
	glCompileShader(fs);

	glGetShaderiv(fs, GL_COMPILE_STATUS, &success);
	if (!success)
	{
		glGetShaderInfoLog(fs, 512, nullptr, infoLog);
		PLU_CORE_ERROR("ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n{}", infoLog);
		return false;
	}

	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);

	glLinkProgram(program);

	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success) {
		glGetProgramInfoLog(program, 512, nullptr, infoLog);
		PLU_CORE_ERROR("ERROR::SHADER::PROGRAM::LINKING_FAILED\n{}", infoLog);
		return false;
	}

	glDeleteShader(vs);
	glDeleteShader(fs);

	UnloadProgram();
	mProgramID = program;
	mUniformLocationCache.Clear();
	PLU_CORE_TRACE("Program recompiled OpenGL ID {}", mProgramID);
	SaveBinary();
	return true;
}

void Plu::ShaderProgram::UnloadProgram()
{
	glDeleteProgram(mProgramID);
	// Zwolnione ID może zostać oddane przez GL nowemu programowi — cache bindowania by wtedy
	// błędnie pominął glUseProgram dla "już zbindowanego" ID.
	ResetBindCache();
	mProgramID = 0;
	mHasBoneMatricesBlock = -1;
	mHasInstanceDataBlock = -1;
	mHasVisibleIndexBlock = -1;
}

bool Plu::ShaderProgram::HasBoneMatricesBlock()
{
	if (!IsLoaded()) return false;
	if (mHasBoneMatricesBlock < 0) {
		mHasBoneMatricesBlock =
			glGetProgramResourceIndex(mProgramID, GL_SHADER_STORAGE_BLOCK, "BoneMatrices") != GL_INVALID_INDEX ? 1 : 0;
	}
	return mHasBoneMatricesBlock == 1;
}

bool Plu::ShaderProgram::HasInstanceDataBlock()
{
	if (!IsLoaded()) return false;
	if (mHasInstanceDataBlock < 0) {
		mHasInstanceDataBlock =
			glGetProgramResourceIndex(mProgramID, GL_SHADER_STORAGE_BLOCK, "InstanceMatrices") != GL_INVALID_INDEX ? 1 : 0;
	}
	return mHasInstanceDataBlock == 1;
}

bool Plu::ShaderProgram::HasVisibleIndexBlock()
{
	if (!IsLoaded()) return false;
	if (mHasVisibleIndexBlock < 0) {
		mHasVisibleIndexBlock =
			glGetProgramResourceIndex(mProgramID, GL_SHADER_STORAGE_BLOCK, "VisibleInstanceIndices") != GL_INVALID_INDEX ? 1 : 0;
	}
	return mHasVisibleIndexBlock == 1;
}

bool Plu::ShaderProgram::BinaryExists() const
{
	PathW outPath = GetGlobalShaderCacheWriter()->GetShaderCacheDirectory();
	outPath += L"/" + BuildShaderCacheName().ToWide() + L"/";
	outPath += StringW::FromInt(Uuid.getUUID()) + PLU_BINARY_EXT_W;
	std::filesystem::create_directories(outPath.GetParentPath().CStr());
	return std::filesystem::exists(outPath.CStr());
}

void Plu::ShaderProgram::LoadFromBinary()
{
	PathW cachePath = GetGlobalShaderCacheWriter()->GetShaderCacheDirectory();
	cachePath += L"/" + BuildShaderCacheName().ToWide() + L"/";
	cachePath += StringW::FromInt(Uuid.getUUID()) + PLU_BINARY_EXT_W;
	if (!std::filesystem::exists(cachePath.CStr())) {
		Recompile();
		return;
	}
	BinaryFileReader reader(cachePath);

	if (!reader.IsOpen()) return;

	GLenum binaryFormat;
	reader.Read(&binaryFormat, sizeof(binaryFormat));

	std::vector<char> binary(std::filesystem::file_size(cachePath.CStr()) - sizeof(binaryFormat));
	reader.Read(binary.data(), static_cast<long long>(binary.size()));
	reader.CloseFile();

	GLuint program = glCreateProgram();
	glProgramBinary(program, binaryFormat, binary.data(), static_cast<GLsizei>(binary.size()));

	GLint success = 0;
	glGetProgramiv(program, GL_LINK_STATUS, &success);
	if (!success)
	{
		glDeleteProgram(program);
		PLU_CORE_ERROR("ERROR::SHADER::PROGRAM::BINARY_FAILED");
		Recompile();
		return;
	}
	mProgramID = program;
	mHasBoneMatricesBlock = -1;
	mHasInstanceDataBlock = -1;
	mHasVisibleIndexBlock = -1;
	PLU_CORE_INFO("Loaded program with UUID {} from binary with new ID {}", Uuid.getUUID(), mProgramID);
}
