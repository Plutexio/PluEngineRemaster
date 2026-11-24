//
// Created by Plutex on 6/26/26.
//

#ifndef PLUENGINE_GIZMOUTILS_H
#define PLUENGINE_GIZMOUTILS_H

#include "PluEngine/Core.h"
#include "imgui.h"
#include "ImGuizmo/ImGuizmo.h"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtc/quaternion.hpp"
#include "PluEngine/PluTypes.h"
#include "PluEngine/Render/RenderingInterfaces.h"
#include "PluEngine/Render/RenderUtils.h"
#include "GizmoUtils.generated.h"

namespace Plu
{
    PLU_ENUM(PyNamespace=Plu)
    enum class GizmoOperationSpace
    {
        LOCAL,
        WORLD
    };

    PLU_ENUM(PyNamespace=Plu)
    enum class GizmoOperation
    {
        TRANSLATE,
        ROTATE,
        SCALE
    };

    inline ImGuizmo::MODE PluGizmoModeToImGuizmoMode(GizmoOperationSpace space)
    {
        switch (space) {
            case GizmoOperationSpace::LOCAL:
                return ImGuizmo::LOCAL;
                break;
            case GizmoOperationSpace::WORLD:
                return ImGuizmo::WORLD;
                break;
            default: ;
        }
        return ImGuizmo::WORLD;
    }

    inline ImGuizmo::OPERATION PluGizmoOperationToImGuizmoOperation(GizmoOperation op)
    {
        switch (op) {
            case GizmoOperation::TRANSLATE:
                return ImGuizmo::TRANSLATE;
                break;
            case GizmoOperation::ROTATE:
                return ImGuizmo::ROTATE;
                break;
            case GizmoOperation::SCALE:
                return ImGuizmo::SCALE;
                break;
        }
        return ImGuizmo::TRANSLATE;
    }

    // View matrix of a viewport camera (location + Euler rotation in degrees) — the convention the
    // old renderer used, so gizmos and overlays line up with what the scene renderer draws.
    inline Matrix4 GetEditorCameraViewMatrix(IRendererCamera* camera)
    {
        if (!camera) return Matrix4(1.0f);
        return glm::inverse(
            glm::translate(Matrix4(1.0f), camera->GetCameraLocation()) *
            glm::mat4_cast(glm::quat(glm::radians(camera->GetCameraRotation()))));
    }

    // Projection matrix of a viewport camera for a viewport with the given aspect ratio (width /
    // height). Perspective or orthographic per CameraOptions, clip planes kCameraNearClip/FarClip.
    // GL clip-space convention (y up, depth -1..1), which is what ImGuizmo expects.
    inline Matrix4 GetEditorCameraProjectionMatrix(IRendererCamera* camera, float aspect)
    {
        if (camera && camera->GetCameraOptions()->CameraPerspective == PerspectiveType::Orthographic) {
            const float halfWidth = camera->GetCameraOptions()->OrthoWidth / 2.0f;
            return glm::ortho(-halfWidth, halfWidth, -halfWidth, halfWidth, kCameraNearClip, kCameraFarClip);
        }
        const float fov = camera ? camera->GetCameraOptions()->FieldOfView : 45.0f;
        return glm::perspective(glm::radians(fov), aspect, kCameraNearClip, kCameraFarClip);
    }
}

#endif //PLUENGINE_GIZMOUTILS_H
