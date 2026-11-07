//
// Created by Plutex on 2026-08-02.
//

#ifndef PLUENGINE_GLUNIFORMBUFFER_H
#define PLUENGINE_GLUNIFORMBUFFER_H

#include <glad/glad.h>
#include <type_traits>
#include "PluEngine/Core.h"
#include "PluBase/Log.h"

namespace Plu
{
    // GPU Uniform Buffer Object (UBO) wrapper holding exactly one block of type T.
    //
    // Same shape as ShaderStorageBuffer (move-only header-only GL resource wrapper with
    // Create / Update / BindBase / Destroy), but for `layout(std140, binding = N) uniform`
    // blocks: small, read-only, frequently rewritten data such as the per-frame shadow
    // parameters. A UBO is the right tool there — its contents are visible to every shader
    // program at once, so the engine stops re-setting the same uniforms per program per frame.
    //
    // T must be a trivially-copyable POD laid out to match the shader's std140 block.
    // std140 is stricter than std430: every member is aligned to its own size rounded up to
    // vec4 (16 B) for vec3/vec4/mat*, and array elements are ALWAYS padded to 16 B. Declare
    // vec4/mat4 members first and scalars last, and static_assert the offsets you rely on.
    template <typename T>
    class UniformBuffer
    {
        static_assert(std::is_trivially_copyable_v<T>,
                      "UniformBuffer<T> requires a trivially-copyable, std140-compatible T");

    public:
        UniformBuffer()
            : BufferID(0)
            , Usage(GL_DYNAMIC_DRAW)
        {
        }

        ~UniformBuffer()
        {
            Destroy();
        }

        // Prevent copying
        UniformBuffer(const UniformBuffer&) = delete;
        UniformBuffer& operator=(const UniformBuffer&) = delete;

        // Allow moving
        UniformBuffer(UniformBuffer&& Other) noexcept
            : BufferID(Other.BufferID)
            , Usage(Other.Usage)
        {
            Other.BufferID = 0;
            Other.Usage = GL_DYNAMIC_DRAW;
        }

        UniformBuffer& operator=(UniformBuffer&& Other) noexcept
        {
            if (this != &Other)
            {
                Destroy();

                BufferID = Other.BufferID;
                Usage = Other.Usage;

                Other.BufferID = 0;
                Other.Usage = GL_DYNAMIC_DRAW;
            }
            return *this;
        }

        // Allocate storage for one T. InitialData may be null to leave it uninitialised.
        bool Create(const T* InitialData = nullptr, GLenum InUsage = GL_DYNAMIC_DRAW)
        {
            Destroy();

            Usage = InUsage;

            glGenBuffers(1, &BufferID);
            glBindBuffer(GL_UNIFORM_BUFFER, BufferID);
            glBufferData(GL_UNIFORM_BUFFER, sizeof(T), InitialData, Usage);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            return CheckGLError("UniformBuffer::Create");
        }

        // Overwrite the whole block (glBufferSubData — no reallocation, the binding survives).
        bool Update(const T& Data)
        {
            if (!IsValid())
            {
                return false;
            }

            glBindBuffer(GL_UNIFORM_BUFFER, BufferID);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(T), &Data);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);

            return CheckGLError("UniformBuffer::Update");
        }

        // Bind to the generic GL_UNIFORM_BUFFER target (for CPU-side updates).
        void Bind() const
        {
            glBindBuffer(GL_UNIFORM_BUFFER, BufferID);
        }

        void Unbind() const
        {
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
        }

        // Bind to an indexed binding point (matches `layout(std140, binding = N)` in the shader).
        // Unlike per-program uniforms this reaches every program that declares the block, so it
        // is set once per frame rather than once per program.
        void BindBase(GLuint BindingPoint) const
        {
            glBindBufferBase(GL_UNIFORM_BUFFER, BindingPoint, BufferID);
        }

        // Getters
        [[nodiscard]] GLuint GetID() const { return BufferID; }
        [[nodiscard]] bool IsValid() const { return BufferID != 0; }
        [[nodiscard]] GLenum GetUsage() const { return Usage; }
        [[nodiscard]] static constexpr size_t GetBlockSize() { return sizeof(T); }

        // Cleanup
        void Destroy()
        {
            if (BufferID != 0)
            {
                glDeleteBuffers(1, &BufferID);
                BufferID = 0;
            }
            Usage = GL_DYNAMIC_DRAW;
        }

    private:
        GLuint BufferID;
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

#endif //PLUENGINE_GLUNIFORMBUFFER_H
