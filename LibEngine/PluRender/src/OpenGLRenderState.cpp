//
// Created by Plutex on 7/3/26.
//

#include "PluEngine/Render/OpenGLRenderState.h"

#include "glad/glad.h"
#include "PluBase/Log.h"

namespace
{
    void APIENTRY glDebugOutput(GLenum source, GLenum type, unsigned int id, GLenum severity,
                                GLsizei length, const char* message, const void* userParam)
    {
        // Ignoruj nieistotne kody błędów
        if (id == 131185 || id == 131218 || id == 131204) return;

        PLU_CORE_ERROR("---------------");
        PLU_CORE_ERROR("Debug message ( {} ): {}", id, message);

        switch (severity)
        {
            case GL_DEBUG_SEVERITY_HIGH:         PLU_CORE_ERROR("Severity: HIGH"); break;
            case GL_DEBUG_SEVERITY_MEDIUM:       PLU_CORE_ERROR("Severity: MEDIUM"); break;
            case GL_DEBUG_SEVERITY_LOW:          PLU_CORE_ERROR("Severity: LOW"); break;
            case GL_DEBUG_SEVERITY_NOTIFICATION: PLU_CORE_ERROR("Severity: NOTIFICATION"); break;
        }
    }
}

void Plu::OpenGLRenderState::Initialize()
{
    int flags;
    glGetIntegerv(GL_CONTEXT_FLAGS, &flags);
    if (flags & GL_CONTEXT_FLAG_DEBUG_BIT) {
        glEnable(GL_DEBUG_OUTPUT);
        glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS); // Dzięki temu callback wykona się natychmiast
        glDebugMessageCallback(glDebugOutput, nullptr);
        glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
    }

    glViewport(0, 0, 1, 1);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    mInitialized = true;
    PLU_CORE_TRACE("OpenGL render state initialized (depth test, blending, polygon mode)");
}
