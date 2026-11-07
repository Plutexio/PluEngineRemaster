//
// Created by Plutex on 10.01.2026.
//

#ifndef PLUENGINE_STATICMESH_H
#define PLUENGINE_STATICMESH_H
#include "PluEngine/PluTypes.h"
#include "PluEngine/Core/IAssetData.h"
#include "glad/glad.h"
#include "StaticMesh.generated.h"
#include "PluEngine/Core/BoundingBox.h"

namespace Plu
{
    struct IStaticMeshCollisionData;
    // NoVirtualClass: POD wierzchołka wysyłany surowo na GPU (offsetof/sizeof) —
    // nie może dostać vtable, więc GetClass() zostaje niewirtualne.
    PLU_STRUCT(NoVirtualClass)
    struct PLUASSETTYPES_API Vertex
    {
        REFLECTION_BODY_VERTEX()

        Vec3 Position;
        UInt32 Normal;
        UInt16 UV[2];
        UInt32 Color;
        UInt32 Tangent;
    };

    PLU_STRUCT()
    struct PLUASSETTYPES_API MeshData
    {
        REFLECTION_BODY_MESHDATA()

        DynamicArray<Vertex> Vertices;
        DynamicArray<UInt32> Indices;
        UInt16 MaterialIndex;
    };

    PLU_STRUCT(PyExport)
    struct PLUASSETTYPES_API StaticMesh : IAssetData
    {
        REFLECTION_BODY_STATICMESH()

        PLU_PROPERTY()
        MeshData StaticMeshData; //This we load

        TOwningPointer<IStaticMeshCollisionData> CollisionData;
        String CollisionName;

        //This we do when needed
        PLU_PROPERTY()
        UInt32 VBO;

        PLU_PROPERTY()
        UInt32 VAO;

        PLU_PROPERTY()
        UInt32 EBO;

        PLU_PROPERTY()
        bool IsLoaded;

        PLU_PROPERTY(PyExport)
        UInt32 VertexCount;

        PLU_PROPERTY(PyExport)
        UInt32 IndexCount;
    };

    inline void SetupStaticMeshGL(MeshData* meshData, StaticMesh* staticMesh)
    {
        // Generuj bufory
        glGenVertexArrays(1, &staticMesh->VAO);
        glGenBuffers(1, &staticMesh->VBO);
        glGenBuffers(1, &staticMesh->EBO);

        // Binduj VAO
        glBindVertexArray(staticMesh->VAO);

        // Vertex Buffer
        glBindBuffer(GL_ARRAY_BUFFER, staticMesh->VBO);
        glBufferData(GL_ARRAY_BUFFER,
                     meshData->Vertices.Size() * sizeof(Vertex),
                     meshData->Vertices.Data(),
                     GL_STATIC_DRAW);

        // Element Buffer
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, staticMesh->EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
                     meshData->Indices.Size() * sizeof(UInt32),
                     meshData->Indices.Data(),
                     GL_STATIC_DRAW);

        // Vertex Attributes

        // Location 0: Position (Vec3 - 3 floats)
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, Position)));

        // Location 1: Normal (MaxUInt32 - packed 10_10_10_2)
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_INT_2_10_10_10_REV, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, Normal)));

        // Location 2: UV (2x MaxUInt16 - packed 16-bit)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_TRUE, sizeof(Vertex), reinterpret_cast<void *>(offsetof(Vertex, UV)));

        // Location 3: Color (MaxUInt32 - packed RGBA8)
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, Color)));

        // Location 4: Tangent (packed 10_10_10_2 — xyz tangent, w = handedness/znak bitangentu)
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 4, GL_INT_2_10_10_10_REV, GL_TRUE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, Tangent)));

        // Unbind
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);

        // Zapisz licznik wierzchołków i indeksów (draw używa IndexCount)
        staticMesh->VertexCount = meshData->Vertices.Size();
        staticMesh->IndexCount = meshData->Indices.Size();
        staticMesh->IsLoaded = true;
    }

    // Opcjonalna funkcja do czyszczenia
    inline void CleanupStaticMeshGL(StaticMesh* staticMesh)
    {
        if (staticMesh->VBO != 0)
        {
            glDeleteBuffers(1, static_cast<GLuint *>(&staticMesh->VBO));
            staticMesh->VBO = 0;
        }

        if (staticMesh->EBO != 0)
        {
            glDeleteBuffers(1, static_cast<GLuint *>(&staticMesh->EBO));
            staticMesh->EBO = 0;
        }

        if (staticMesh->VAO != 0)
        {
            glDeleteVertexArrays(1, static_cast<GLuint *>(&staticMesh->VAO));
            staticMesh->VAO = 0;
        }

        staticMesh->VertexCount = 0;
        staticMesh->IndexCount = 0;
        staticMesh->IsLoaded = false;
    }

    // Funkcja do renderowania

    // Wariant instanced: jeden glDrawElementsInstanced rysujący instanceCount instancji naraz
    // (dane per-instancja idą przez SSBO "InstanceMatrices", indeksowane gl_InstanceID + uniform
    // instanceBaseIndex — patrz Renderer::RenderSnapshot). Jedno OnStaticMeshRender, nie N: to
    // flaga żywotności dla eviction (RenderingManager), nie licznik populacji instancji.
}

#endif //PLUENGINE_STATICMESH_H