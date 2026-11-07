//
// Created by Plutex on 2026-02-07.
//

// StaticMeshImporter.cpp
#include "PluEngine/AssetPipeline/StaticMesh/StaticMeshAssimpLoader.h"
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>
#include <glm/matrix.hpp>

#include "PluEngine/AssetCore/EngineAssetManager.h"
#include "PluEngine/AssetPipeline/Mesh/MeshProcessing.h"
#include "PluEngine/AssetPipeline/Textures/TextureImporter.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/PluPaths.h"
#include "PluEngine/Timer.h"

namespace Plu {
namespace {
// Wyciąga embedded tekstury (aiScene::mTextures) i zapisuje każdą jako osobny
// asset .plubin.
void ExtractEmbeddedTextures(const aiScene *scene, const PathW &outDir,
                             const String &modelStem,
                             TUsePointer<EngineAssetManager> assetManager) {
  if (scene->mNumTextures == 0)
    return;

  PLU_CORE_INFO("Model has {} embedded texture(s)", scene->mNumTextures);

  for (UInt32 i = 0; i < scene->mNumTextures; i++) {
    const aiTexture *tex = scene->mTextures[i];

    String texName = modelStem + String("_tex") + String::FromInt(i);
    PathW outPath =
        outDir / (StringW::FromNarrow(texName.CStr()) + PLU_BINARY_EXT_W);

    bool ok = false;
    if (tex->mHeight == 0) {
      // Skompresowany blob (PNG/JPG/...): mWidth = rozmiar w bajtach, pcData =
      // dane
      ok = TextureImport::ImportTextureFromMemory(
          reinterpret_cast<const unsigned char *>(tex->pcData), tex->mWidth,
          outPath);
    } else {
      // Surowe, nieskompresowane ARGB8888 (aiTexel) — rzadkie; na razie
      // pomijamy
      PLU_CORE_WARN("Embedded texture {} ('{}') is raw/uncompressed — skipping "
                    "(not supported yet)",
                    i, tex->mFilename.C_Str());
    }

    if (ok) {
      assetManager->LoadAssetDescriptor(outPath.ToString().ToNarrow());
      PLU_CORE_INFO("Extracted embedded texture {} -> {}", i,
                    String::FromWide(outPath.CStr()).CStr());
    }
  }
}
} // namespace

namespace MeshImporter {
bool ImportStaticMesh(StaticMeshImportProps props, PathW import, PathW outDir,
                      TUsePointer<EngineAssetManager> assetManager) {
  PLU_PROFILE_SCOPE("ImportStaticMesh");

  Path pathNarrow = import.ToString().ToNarrow();
  PLU_CORE_INFO("Importing mesh from: {}", pathNarrow.CStr());

  MeshProcessing::EnsureAssimpLoggerAttached();
  Assimp::Importer importer;

  UInt32 flags = aiProcess_Triangulate | // bez tego quady/n-gony rozjeżdżają
                                         // topologię indeksów
                 aiProcess_JoinIdenticalVertices | aiProcess_FlipWindingOrder |
                 aiProcess_CalcTangentSpace;

  if (props.FlipUVs) {
    flags |= aiProcess_FlipUVs;
  }

  if (props.GenerateNormals) {
    flags |= aiProcess_GenNormals;
  }
  const aiScene *scene;
  try {
    scene = importer.ReadFile(pathNarrow.CStr(), flags);
  } catch (...) {
    PLU_ERROR("Error importing mesh at: {}", pathNarrow.CStr());
    return false;
  }

  if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
      !scene->mRootNode) {
    PLU_CORE_ERROR("Assimp Error: {}", importer.GetErrorString());
    return false;
  }

  // Wyciągnij embedded tekstury (jeśli są) jako osobne assety obok meshów
  ExtractEmbeddedTextures(
      scene, outDir, String::FromWide(import.GetStem().CStr()), assetManager);

  DynamicArray<MeshData> meshes;
  DynamicArray<String> meshNames;

  // Macierz identycznościowa jako początkowa transformacja
  glm::mat4 identityTransform = glm::mat4(1.0f);

  MeshProcessing::ProcessNode(scene->mRootNode, scene, meshes, props.Scale,
                              props.FlipUVs, props.Merge, identityTransform,
                              meshNames);

  if (meshes.Size() == 0) {
    PLU_CORE_ERROR("No meshes found in file!");
    return false;
  }

  // Zapisz meshe
  String fileName = String::FromWide(import.GetStem().CStr());

  if (props.Merge) {
    // Jeden plik z nazwą pliku .fbx
    StaticMesh staticMesh;
    staticMesh.StaticMeshData = meshes[0];
    staticMesh.IsLoaded = false;
    staticMesh.VertexCount = 0;
    staticMesh.VBO = 0;
    staticMesh.VAO = 0;
    staticMesh.EBO = 0;

    PathW outPath = outDir / (import.GetStem() + (PLU_BINARY_EXT_W));

    if (!SaveStaticMesh(outPath, &staticMesh)) {
      PLU_CORE_ERROR("Failed to save merged mesh!");
      return false;
    }

    PLU_CORE_INFO("Saved merged mesh: {} ({} vertices, {} indices)",
                  String::FromWide(outPath.CStr()).CStr(),
                  meshes[0].Vertices.Size(), meshes[0].Indices.Size());

    assetManager->LoadAssetDescriptor(outPath.ToString().ToNarrow());
  } else {
    // Wiele plików z nazwami modeli
    for (UInt32 i = 0; i < meshes.Size(); i++) {
      StaticMesh staticMesh;
      staticMesh.StaticMeshData = meshes[i];
      staticMesh.IsLoaded = false;
      staticMesh.VertexCount = 0;
      staticMesh.VBO = 0;
      staticMesh.VAO = 0;
      staticMesh.EBO = 0;

      // Użyj nazwy z Assimp
      String meshName = meshNames[i];

      // Usuń niedozwolone znaki z nazwy pliku
      meshName.Replace("/", "_");
      meshName.Replace("\\", "_");
      meshName.Replace(":", "_");
      meshName.Replace("|", "_");
      meshName.Replace(".", "_");

      PathW outPath =
          outDir / (StringW::FromNarrow(meshName.CStr()) + (PLU_BINARY_EXT_W));

      if (!SaveStaticMesh(outPath, &staticMesh)) {
        PLU_CORE_ERROR("Failed to save mesh {}!", meshName.CStr());
        continue;
      }

      PLU_CORE_INFO("Saved mesh: {} ({} vertices, {} indices)",
                    String::FromWide(outPath.CStr()).CStr(),
                    meshes[i].Vertices.Size(), meshes[i].Indices.Size());

      assetManager->LoadAssetDescriptor(outPath.ToString().ToNarrow());
    }
  }

  return true;
}

bool SaveStaticMesh(PathW path, StaticMesh *mesh) {
  BinaryFileWriter writer(path);

  if (!writer.IsOpen()) {
    PLU_CORE_ERROR("Failed to open file for writing: {}",
                   writer.GetLastError().CStr());
    return false;
  }

  // Magic number i wersja
  UInt32 magic = 0x41554C50; // 'PLUA'
  UInt32 version = 3; // v2: Vertex zawiera spakowany Tangent, v3 collision data
  writer.Write(&magic, sizeof(UInt32));
  writer.Write(&version, sizeof(UInt32));

  // Typ assetu
  String typeName = "StaticMesh";
  writer.WriteString(typeName);

  UInt64 uuid = mesh->Uuid;
  writer.Write(&uuid, sizeof(UInt64));

  // Zapisz MeshData
  // Vertices — zapis per-pole (jawnie), żeby format nie zależał od
  // layoutu/paddingu Vertex
  UInt32 vertexCount = mesh->StaticMeshData.Vertices.Size();
  writer.Write(&vertexCount, sizeof(UInt32));
  for (UInt32 i = 0; i < vertexCount; i++) {
    const Vertex &v = mesh->StaticMeshData.Vertices[i];
    writer.Write(&v.Position, sizeof(Vec3));
    writer.Write(&v.Normal, sizeof(UInt32));
    writer.Write(&v.UV[0], sizeof(UInt16));
    writer.Write(&v.UV[1], sizeof(UInt16));
    writer.Write(&v.Color, sizeof(UInt32));
    writer.Write(&v.Tangent,
                 sizeof(UInt32)); // v2: spakowany tangent 10_10_10_2
  }

  // Indices
  UInt32 indexCount = mesh->StaticMeshData.Indices.Size();
  writer.Write(&indexCount, sizeof(UInt32));
  writer.WriteArray<UInt32>(mesh->StaticMeshData.Indices.Data(), indexCount);

  // Material index
  writer.Write(&mesh->StaticMeshData.MaterialIndex, sizeof(UInt16));

  // Collision shapes
  String toWrite = "NoCollision";
  if (mesh->CollisionData) {
    toWrite = mesh->CollisionName;
  }
  writer.WriteString(toWrite);
  return true;
}

bool LoadStaticMesh(PathW path, StaticMesh *outMesh) {
  BinaryFileReader reader(path);

  if (!reader.IsOpen()) {
    PLU_CORE_ERROR("Failed to open file: {}", reader.GetLastError().CStr());
    return false;
  }

  // Sprawdź magic number i wersję
  UInt32 magic = 0;
  UInt32 version = 0;
  reader.Read(&magic, sizeof(UInt32));
  reader.Read(&version, sizeof(UInt32));

  if (magic != 0x41554C50) {
    PLU_ERROR("File {} has invalid magic!",
              String::FromWide(path.CStr()).CStr());
    reader.CloseFile();
    return false;
  }
  if (version < 2) {
    PLU_ERROR("File {} has invalid version!",
              path.ToString().ToNarrow().CStr());
    reader.CloseFile();
    return false;
  }

  // Typ assetu
  String typeName;
  reader.ReadString(typeName);

  if (typeName != "StaticMesh") {
    PLU_ERROR("File {} is not a StaticMesh!",
              String::FromWide(path.CStr()).CStr());
    reader.CloseFile();
    return false;
  }

  UInt64 uuid;
  reader.Read(&uuid, sizeof(UInt64));
  outMesh->Uuid = uuid;

  // Wczytaj MeshData
  // Vertices — odczyt per-pole, symetrycznie do zapisu
  UInt32 vertexCount = 0;
  reader.Read(&vertexCount, sizeof(UInt32));
  outMesh->StaticMeshData.Vertices.Resize(vertexCount);
  for (UInt32 i = 0; i < vertexCount; i++) {
    Vertex &v = outMesh->StaticMeshData.Vertices[i];
    reader.Read(&v.Position, sizeof(Vec3));
    reader.Read(&v.Normal, sizeof(UInt32));
    reader.Read(v.UV, sizeof(UInt16) * 2);
    reader.Read(&v.Color, sizeof(UInt32));
    reader.Read(&v.Tangent, sizeof(UInt32)); // v2: spakowany tangent 10_10_10_2
  }

  // Indices
  UInt32 indexCount = 0;
  reader.Read(&indexCount, sizeof(UInt32));
  outMesh->StaticMeshData.Indices.Resize(indexCount);
  reader.ReadArray<UInt32>(outMesh->StaticMeshData.Indices.Data(), indexCount);

  // Material index
  reader.Read(&outMesh->StaticMeshData.MaterialIndex, sizeof(UInt16));

  // Collision shapes (optional — older files without this block are handled
  // gracefully)
  if (version == 2) {
    PLU_CORE_WARN("StaticMesh collision have been ignored because of the old "
                  "version, resave the mesh to use the new collision system");
  } else if (version == 3) {
    String collisionName;
    reader.ReadString(collisionName);
    if (collisionName != "NoCollision") {
      outMesh->CollisionName = collisionName;
    }
  }

  reader.CloseFile();

  // Zainicjalizuj pozostałe pola
  outMesh->IsLoaded = false;
  outMesh->VertexCount = 0;
  outMesh->VBO = 0;
  outMesh->VAO = 0;
  outMesh->EBO = 0;
  return true;
}
} // namespace MeshImporter
} // namespace Plu
