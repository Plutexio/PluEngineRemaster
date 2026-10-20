//
// Created by Plutex on 2026-07-06.
//

#ifndef PLUENGINE_GLSHADERSTORAGEBUFFER_H
#define PLUENGINE_GLSHADERSTORAGEBUFFER_H

#include <glad/glad.h>
#include <type_traits>
#include "PluEngine/Core.h"
#include "PluEngine/Log.h"
#include "PluSTL/Array/Array.h"

// When glm is reachable, detect 3-component glm vectors so we can reject them as T:
// vec3 is 12 B in C++ but std430 gives an array of vec3 a 16 B stride, so a raw upload
// silently misaligns from the second element on. Guarded so the wrapper keeps no hard glm dependency.
#if __has_include(<glm/fwd.hpp>)
#include <glm/fwd.hpp>
#define PLU_SSBO_HAS_GLM 1
#else
#define PLU_SSBO_HAS_GLM 0
#endif

namespace Plu
{
#if PLU_SSBO_HAS_GLM
    namespace detail
    {
        template <typename U> struct IsGlmVec3 : std::false_type {};
        template <typename S, glm::qualifier Q>
        struct IsGlmVec3<glm::vec<3, S, Q>> : std::true_type {};
    }
#endif

    // GPU Shader Storage Buffer Object (SSBO) wrapper, templated on the element type.
    //
    // Mirrors the style of FrameBuffer / Texture (move-only GL resource wrapper with
    // Create / Bind / Update / Destroy), but is a plain header-only template rather than a
    // reflected EngineObject: a templated class cannot carry a REFLECTION_BODY, so each
    // instantiation is an ordinary C++ type. Store one wherever you would store a Texture.
    //
    // T must be a trivially-copyable POD laid out to match the shader's std430 block.
    // Element size is sizeof(T); mind std430 alignment rules when declaring T
    // (e.g. Vec3 occupies 16 bytes in std430 — pad accordingly).
    template <typename T>
    class ShaderStorageBuffer
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "ShaderStorageBuffer<T> requires a trivially-copyable, std430-compatible T");

#if PLU_SSBO_HAS_GLM
        static_assert(!detail::IsGlmVec3<T>::value,
                      "ShaderStorageBuffer<Vec3>: a 3-component glm vector is 12 B in C++ but std430 gives "
                      "an array of vec3 a 16 B stride, so a raw upload misaligns from the second element on. "
                      "Use Vec4, or wrap it in a padded struct { Vec3 v; float _pad; }.");
#endif

    public:
        ShaderStorageBuffer()
            : BufferID(0)
            , Count(0)
            , Usage(GL_DYNAMIC_DRAW)
        {
        }

        ~ShaderStorageBuffer()
        {
            Destroy();
        }

        // Prevent copying
        ShaderStorageBuffer(const ShaderStorageBuffer&) = delete;
        ShaderStorageBuffer& operator=(const ShaderStorageBuffer&) = delete;

        // Allow moving
        ShaderStorageBuffer(ShaderStorageBuffer&& Other) noexcept
            : BufferID(Other.BufferID)
            , Count(Other.Count)
            , Usage(Other.Usage)
        {
            Other.BufferID = 0;
            Other.Count = 0;
            Other.Usage = GL_DYNAMIC_DRAW;
        }

        ShaderStorageBuffer& operator=(ShaderStorageBuffer&& Other) noexcept
        {
            if (this != &Other)
            {
                Destroy();

                BufferID = Other.BufferID;
                Count = Other.Count;
                Usage = Other.Usage;

                Other.BufferID = 0;
                Other.Count = 0;
                Other.Usage = GL_DYNAMIC_DRAW;
            }
            return *this;
        }

        // Create an SSBO with room for ElementCount elements of T, contents uninitialised.
        // InUsage is a GL usage hint (GL_DYNAMIC_DRAW / GL_STATIC_DRAW / GL_STREAM_DRAW / ...).
        bool Create(Int32 ElementCount, GLenum InUsage = GL_DYNAMIC_DRAW)
        {
            return CreateFromData(nullptr, ElementCount, InUsage);
        }

        // Create an SSBO and upload ElementCount elements from Data (Data may be null to leave uninitialised).
        bool CreateFromData(const T* Data, Int32 ElementCount, GLenum InUsage = GL_DYNAMIC_DRAW)
        {
            if (ElementCount < 0)
            {
                PLU_CORE_ERROR("ShaderStorageBuffer::CreateFromData - Invalid element count: {}", ElementCount);
                return false;
            }

            Destroy();

            Count = ElementCount;
            Usage = InUsage;

            glGenBuffers(1, &BufferID);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, BufferID);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         static_cast<GLsizeiptr>(ElementCount) * sizeof(T),
                         Data, Usage);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

            return CheckGLError("ShaderStorageBuffer::CreateFromData");
        }

        // Convenience: create from a DynamicArray<T>.
        bool CreateFromArray(const DynamicArray<T>& Array, GLenum InUsage = GL_DYNAMIC_DRAW)
        {
            return CreateFromData(Array.Data(), static_cast<Int32>(Array.Size()), InUsage);
        }

        // Bind to the generic GL_SHADER_STORAGE_BUFFER target (for CPU-side updates/reads).
        void Bind() const
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, BufferID);
        }

        void Unbind() const
        {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        }

        // Bind the whole buffer to an indexed binding point (matches `layout(std430, binding = N)` in the shader).
        void BindBase(GLuint BindingPoint) const
        {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, BindingPoint, BufferID);
        }

        // Bind a sub-range [OffsetElements, OffsetElements + ElementCount) to an indexed binding point.
        void BindRange(GLuint BindingPoint, Int32 OffsetElements, Int32 ElementCount) const
        {
            glBindBufferRange(GL_SHADER_STORAGE_BUFFER, BindingPoint, BufferID,
                              static_cast<GLintptr>(OffsetElements) * sizeof(T),
                              static_cast<GLsizeiptr>(ElementCount) * sizeof(T));
        }

        // Update a sub-range of the buffer without reallocating (glBufferSubData).
        // The range [OffsetElements, OffsetElements + ElementCount) must fit inside the buffer.
        bool Update(const T* Data, Int32 ElementCount, Int32 OffsetElements = 0)
        {
            if (!IsValid() || Data == nullptr || ElementCount <= 0)
            {
                return false;
            }

            if (OffsetElements < 0 || OffsetElements + ElementCount > Count)
            {
                PLU_CORE_ERROR("ShaderStorageBuffer::Update - Range [{}, {}) out of bounds (Count = {})",
                               OffsetElements, OffsetElements + ElementCount, Count);
                return false;
            }

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, BufferID);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER,
                            static_cast<GLintptr>(OffsetElements) * sizeof(T),
                            static_cast<GLsizeiptr>(ElementCount) * sizeof(T),
                            Data);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

            return CheckGLError("ShaderStorageBuffer::Update");
        }

        bool Update(const DynamicArray<T>& Array, Int32 OffsetElements = 0)
        {
            return Update(Array.Data(), static_cast<Int32>(Array.Size()), OffsetElements);
        }

        // Replace the entire buffer contents. If ElementCount matches the current size the storage is
        // reused (with an orphaning hint); otherwise the buffer is reallocated.
        bool SetData(const T* Data, Int32 ElementCount)
        {
            if (!IsValid())
            {
                return CreateFromData(Data, ElementCount, Usage);
            }

            if (ElementCount != Count)
            {
                return CreateFromData(Data, ElementCount, Usage);
            }

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, BufferID);
            // Orphan the old storage so the driver can hand us a fresh block without stalling.
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         static_cast<GLsizeiptr>(ElementCount) * sizeof(T),
                         nullptr, Usage);
            glBufferData(GL_SHADER_STORAGE_BUFFER,
                         static_cast<GLsizeiptr>(ElementCount) * sizeof(T),
                         Data, Usage);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

            return CheckGLError("ShaderStorageBuffer::SetData");
        }

        bool SetData(const DynamicArray<T>& Array)
        {
            return SetData(Array.Data(), static_cast<Int32>(Array.Size()));
        }

        // Reallocate to NewCount elements (contents undefined afterwards). No-op if already that size.
        bool Resize(Int32 NewCount)
        {
            if (NewCount < 0)
            {
                PLU_CORE_ERROR("ShaderStorageBuffer::Resize - Invalid count: {}", NewCount);
                return false;
            }
            if (NewCount == Count && IsValid())
            {
                return true;
            }
            return CreateFromData(nullptr, NewCount, Usage);
        }

        // Map the whole buffer into client memory. Returns null on failure.
        // Access is a GL access flag: GL_READ_ONLY / GL_WRITE_ONLY / GL_READ_WRITE. Pair with Unmap().
        T* Map(GLenum Access = GL_WRITE_ONLY)
        {
            if (!IsValid())
            {
                return nullptr;
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, BufferID);
            return static_cast<T*>(glMapBuffer(GL_SHADER_STORAGE_BUFFER, Access));
        }

        // Map a sub-range using explicit access bits (e.g. GL_MAP_WRITE_BIT | GL_MAP_INVALIDATE_RANGE_BIT).
        T* MapRange(Int32 OffsetElements, Int32 ElementCount, GLbitfield AccessBits)
        {
            if (!IsValid() || ElementCount <= 0 || OffsetElements < 0 ||
                OffsetElements + ElementCount > Count)
            {
                return nullptr;
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, BufferID);
            return static_cast<T*>(glMapBufferRange(GL_SHADER_STORAGE_BUFFER,
                                                    static_cast<GLintptr>(OffsetElements) * sizeof(T),
                                                    static_cast<GLsizeiptr>(ElementCount) * sizeof(T),
                                                    AccessBits));
        }

        // Unmap a buffer previously mapped with Map()/MapRange(). Returns false if the store was corrupted.
        bool Unmap() const
        {
            if (!IsValid())
            {
                return false;
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, BufferID);
            GLboolean Ok = glUnmapBuffer(GL_SHADER_STORAGE_BUFFER);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            return Ok == GL_TRUE;
        }

        // Read ElementCount elements back from the GPU into Out (glGetBufferSubData).
        bool GetData(T* Out, Int32 ElementCount, Int32 OffsetElements = 0) const
        {
            if (!IsValid() || Out == nullptr || ElementCount <= 0)
            {
                return false;
            }
            if (OffsetElements < 0 || OffsetElements + ElementCount > Count)
            {
                PLU_CORE_ERROR("ShaderStorageBuffer::GetData - Range [{}, {}) out of bounds (Count = {})",
                               OffsetElements, OffsetElements + ElementCount, Count);
                return false;
            }
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, BufferID);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
                               static_cast<GLintptr>(OffsetElements) * sizeof(T),
                               static_cast<GLsizeiptr>(ElementCount) * sizeof(T),
                               Out);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            return true;
        }

        // Read the whole buffer back into a DynamicArray<T>.
        DynamicArray<T> GetData() const
        {
            DynamicArray<T> Result;
            if (IsValid() && Count > 0)
            {
                Result.Resize(Count);
                GetData(Result.Data(), Count, 0);
            }
            return Result;
        }

        // Getters
        [[nodiscard]] GLuint GetID() const { return BufferID; }
        [[nodiscard]] Int32 GetCount() const { return Count; }
        [[nodiscard]] GLsizeiptr GetSizeBytes() const { return static_cast<GLsizeiptr>(Count) * sizeof(T); }
        [[nodiscard]] GLenum GetUsage() const { return Usage; }
        [[nodiscard]] bool IsValid() const { return BufferID != 0; }
        [[nodiscard]] static constexpr size_t GetElementSize() { return sizeof(T); }

        // Cleanup
        void Destroy()
        {
            if (BufferID != 0)
            {
                glDeleteBuffers(1, &BufferID);
                BufferID = 0;
            }
            Count = 0;
            Usage = GL_DYNAMIC_DRAW;
        }

    private:
        GLuint BufferID;
        Int32 Count;
        GLenum Usage;

        // Drains the GL error queue after an operation, logging any errors. Returns true if clean.
        static bool CheckGLError(const char* Where)
        {
            bool Clean = true;
            GLenum Err;
            while ((Err = glGetError()) != GL_NO_ERROR)
            {
                Clean = false;
                const char* Msg;
                switch (Err)
                {
                    case GL_INVALID_ENUM:      Msg = "INVALID_ENUM"; break;
                    case GL_INVALID_VALUE:     Msg = "INVALID_VALUE"; break;
                    case GL_INVALID_OPERATION: Msg = "INVALID_OPERATION"; break;
                    case GL_OUT_OF_MEMORY:     Msg = "OUT_OF_MEMORY"; break;
                    default:                   Msg = "UNKNOWN"; break;
                }
                PLU_CORE_ERROR("OpenGL Error at {}: {} (0x{:x})", Where, Msg, Err);
            }
            return Clean;
        }
    };
}

#undef PLU_SSBO_HAS_GLM

#endif //PLUENGINE_GLSHADERSTORAGEBUFFER_H
