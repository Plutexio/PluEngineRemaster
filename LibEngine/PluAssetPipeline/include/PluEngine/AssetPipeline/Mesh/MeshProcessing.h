//
// Created by Plutex on 7/6/26.
//
// Shared Assimp -> engine mesh conversion used by both the static-mesh and
// skeletal-mesh importers. The per-vertex packing loop and the node-hierarchy
// walk live here so neither importer duplicates them.
//

#ifndef PLUENGINE_MESHPROCESSING_H
#define PLUENGINE_MESHPROCESSING_H

#include <assimp/scene.h>
#include <glm/glm.hpp>
#include <glm/matrix.hpp>

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "PluBase/Log.h"
#include "PluSTL/Array/Array.h"
#include "PluSTL/String/String.h"
#include "PluEngine/AssetTypes/StaticMesh/StaticMesh.h"   // Vertex

namespace Plu::MeshProcessing
{
    // Vertex-attribute packers (shared by static & skeletal mesh import).
    PLUASSETPIPELINE_API UInt32 PackNormal(const Vec3& normal);
    PLUASSETPIPELINE_API UInt32 PackTangent(const Vec3& tangent, float sign);
    PLUASSETPIPELINE_API UInt16 PackUV(float uv);
    PLUASSETPIPELINE_API UInt32 PackColor(const aiColor4D& color);

    // Assimp -> GLM column-major matrix conversion.
    PLUASSETPIPELINE_API glm::mat4 AssimpToGLM(const aiMatrix4x4& mat);
    PLUASSETPIPELINE_API Vec3 AssimpToGLM(const aiVector3D& vec);
    PLUASSETPIPELINE_API Quaternion AssimpToGLM(const aiQuaternion& quat);

    // One-time bridge of Assimp's internal logger to the engine log.
    PLUASSETPIPELINE_API void EnsureAssimpLoggerAttached();

    // Fills the base Vertex attributes (position/normal/uv/color/tangent) of a single
    // aiMesh into the given buffers. Templated on the vertex type so the skeletal
    // importer can pass SkeletalVertex (a Vertex subclass) and fill bone data in a
    // second pass — only the inherited Vertex fields are touched here.
    //
    // When isMerging, indices are offset by the current vertex count so several meshes
    // can share one buffer; otherwise the mesh is treated as starting at index 0.
    template <typename VertexT>
    void ProcessMeshGeometry(aiMesh* mesh, DynamicArray<VertexT>& vertices,
                             DynamicArray<UInt32>& indices, UInt16& materialIndex,
                             float scale, bool flipUVs, const glm::mat4& transform, bool isMerging)
    {
        // For merge we append after existing verts; separate meshes start their own buffer.
        const UInt32 baseVertexIndex = isMerging ? static_cast<UInt32>(vertices.Size()) : 0;

        // Normal matrix (inverse-transpose) keeps normals/tangents correct under non-uniform
        // scaling in the node hierarchy; tangents/bitangents use the plain model 3x3.
        const glm::mat3 modelMat3 = glm::mat3(transform);
        const glm::mat3 normalMatrix = glm::transpose(glm::inverse(modelMat3));

        for (UInt32 i = 0; i < mesh->mNumVertices; i++)
        {
            VertexT vertex = {};

            glm::vec4 pos = transform * glm::vec4(
                mesh->mVertices[i].x, mesh->mVertices[i].y, mesh->mVertices[i].z, 1.0f);
            vertex.Position = Vec3(pos.x * scale, pos.y * scale, pos.z * scale);

            if (mesh->HasNormals())
            {
                glm::vec3 n = glm::normalize(normalMatrix * glm::vec3(
                    mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z));
                vertex.Normal = PackNormal(Vec3(n.x, n.y, n.z));
            }
            else
            {
                vertex.Normal = PackNormal(Vec3(0, 1, 0));
            }

            if (mesh->HasTextureCoords(0))
            {
                float u = mesh->mTextureCoords[0][i].x;
                float v = flipUVs ? (1.0f - mesh->mTextureCoords[0][i].y) : mesh->mTextureCoords[0][i].y;
                vertex.UV[0] = PackUV(u);
                vertex.UV[1] = PackUV(v);
            }
            else
            {
                vertex.UV[0] = 0;
                vertex.UV[1] = 0;
            }

            if (mesh->HasVertexColors(0))
                vertex.Color = PackColor(mesh->mColors[0][i]);
            else
                vertex.Color = 0xFFFFFFFF;

            if (mesh->HasTangentsAndBitangents())
            {
                glm::vec3 t = glm::normalize(modelMat3 * glm::vec3(
                    mesh->mTangents[i].x, mesh->mTangents[i].y, mesh->mTangents[i].z));
                glm::vec3 b = glm::normalize(modelMat3 * glm::vec3(
                    mesh->mBitangents[i].x, mesh->mBitangents[i].y, mesh->mBitangents[i].z));
                glm::vec3 n = mesh->HasNormals()
                    ? glm::normalize(normalMatrix * glm::vec3(
                        mesh->mNormals[i].x, mesh->mNormals[i].y, mesh->mNormals[i].z))
                    : glm::vec3(0.0f, 1.0f, 0.0f);

                float sign = (glm::dot(glm::cross(n, t), b) < 0.0f) ? -1.0f : 1.0f;
                vertex.Tangent = PackTangent(Vec3(t.x, t.y, t.z), sign);
            }
            else
            {
                vertex.Tangent = PackTangent(Vec3(1, 0, 0), 1.0f);
            }

            vertices.PushBack(vertex);
        }

        for (UInt32 i = 0; i < mesh->mNumFaces; i++)
        {
            aiFace face = mesh->mFaces[i];
            if (face.mNumIndices != 3)
            {
                // Post-triangulation this shouldn't happen; skip lines/points/n-gons so we
                // never push a non-triangle face into the index buffer.
                PLU_CORE_WARN("Face {} has {} indices (not a triangle) — skipping!", i, face.mNumIndices);
                continue;
            }
            for (UInt32 j = 0; j < face.mNumIndices; j++)
                indices.PushBack(baseVertexIndex + face.mIndices[j]);
        }

        materialIndex = static_cast<UInt16>(mesh->mMaterialIndex);
    }

    // Recursively walks the node hierarchy, accumulating world transforms and producing one
    // MeshDataT per mesh (or a single merged MeshDataT when merge is true). MeshDataT must
    // expose .Vertices (DynamicArray<VertexT>), .Indices and .MaterialIndex.
    template <typename MeshDataT>
    void ProcessNode(aiNode* node, const aiScene* scene, DynamicArray<MeshDataT>& meshes,
                     float scale, bool flipUVs, bool merge, const glm::mat4& parentTransform,
                     DynamicArray<String>& meshNames)
    {
        glm::mat4 globalTransform = parentTransform * AssimpToGLM(node->mTransformation);

        for (UInt32 i = 0; i < node->mNumMeshes; i++)
        {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];

            if (merge)
            {
                if (meshes.Size() == 0)
                {
                    meshes.PushBack(MeshDataT());
                    meshNames.PushBack(String("MergedMesh"));
                }
                ProcessMeshGeometry(mesh, meshes[0].Vertices, meshes[0].Indices,
                                    meshes[0].MaterialIndex, scale, flipUVs, globalTransform, true);
            }
            else
            {
                MeshDataT meshData;
                ProcessMeshGeometry(mesh, meshData.Vertices, meshData.Indices,
                                    meshData.MaterialIndex, scale, flipUVs, globalTransform, false);
                meshes.PushBack(meshData);

                String meshName = mesh->mName.length > 0
                    ? String(mesh->mName.C_Str())
                    : String(node->mName.C_Str());
                if (meshName.Length() == 0)
                    meshName = String("Mesh_") + String::FromInt(meshes.Size() - 1);
                meshNames.PushBack(meshName);
            }
        }

        for (UInt32 i = 0; i < node->mNumChildren; i++)
            ProcessNode(node->mChildren[i], scene, meshes, scale, flipUVs, merge, globalTransform, meshNames);
    }
}

#endif //PLUENGINE_MESHPROCESSING_H
