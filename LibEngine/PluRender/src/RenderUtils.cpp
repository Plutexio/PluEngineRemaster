//
// Created by Plutex on 6/9/26.
//

#include "PluEngine/Render/RenderUtils.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "PluEngine/Timer.h"

#include "glm/common.hpp"
#include "glm/geometric.hpp"
#include "glm/gtc/constants.hpp"
#include "glm/ext/matrix_clip_space.hpp"
#include "glm/ext/matrix_transform.hpp"
#include "glm/gtc/matrix_access.hpp"

namespace Plu
{
    void GetFrustumCornersWorldSpace(const Matrix4& proj, const Matrix4& view, Vec3 OutCorners[8])
    {
        const Matrix4 inv = glm::inverse(proj * view);

        int index = 0;
        for (int x = 0; x < 2; x++)
            for (int y = 0; y < 2; y++)
                for (int z = 0; z < 2; z++)
                {
                    Vec4 pt = inv * Vec4(2.0f * x - 1.0f, 2.0f * y - 1.0f, 2.0f * z - 1.0f, 1.0f);
                    OutCorners[index++] = Vec3(pt) / pt.w;
                }
    }

    DynamicArray<Vec3> GetFrustumCornersWorldSpace(const Matrix4& proj, const Matrix4& view)
    {
        Vec3 corners[8];
        GetFrustumCornersWorldSpace(proj, view, corners);

        DynamicArray<Vec3> result;
        result.Reserve(8);
        for (const Vec3& c : corners)
            result.PushBack(c);
        return result;
    }

    Int32 ComputeAutoPcfTapCount(float PcfRadiusTexels)
    {
        if (PcfRadiusTexels <= 0.0f) return 1;

        const float needed = glm::pi<float>() * PcfRadiusTexels * PcfRadiusTexels;
        return glm::clamp(static_cast<Int32>(std::ceil(needed)), 1, kMaxShadowPcfTaps);
    }

    Matrix4 GetCascadeProjectionMatrix(float fovYRadians, float aspect, float nearPlane, float farPlane)
    {
        return glm::perspective(fovYRadians, aspect, nearPlane, farPlane);
    }

    void ComputeCascadeResolutions(Int32 BaseResolution, Int32 Count, Int32 Falloff, Int32* OutResolutions)
    {
        if (Count <= 0 || !OutResolutions) return;

        const Int32 base = std::max(BaseResolution, kMinCascadeResolution);
        for (Int32 i = 0; i < Count; i++)
        {
            // Shift rather than divide, so the result stays a power of two — the atlas shelf
            // packing below relies on that to fill each shelf exactly.
            const Int32 steps = (Falloff > 0) ? (i / Falloff) : 0;
            // Guard the shift itself: >> 31 is UB territory and a Falloff of 1 with many cascades
            // would reach it long before the clamp had a chance to run.
            const Int32 resolution = (steps >= 16) ? kMinCascadeResolution : (base >> steps);
            OutResolutions[i] = std::max(resolution, kMinCascadeResolution);
        }
    }

    void BuildShadowAtlasLayout(const Int32* Resolutions, Int32 Count,
                                ShadowAtlasRect* OutRects, Int32& OutWidth, Int32& OutHeight)
    {
        OutWidth  = 0;
        OutHeight = 0;
        if (Count <= 0 || !Resolutions || !OutRects) return;

        Int32 width = 0;
        for (Int32 i = 0; i < Count; i++)
            width = std::max(width, Resolutions[i]);
        if (width <= 0) return;

        Int32 shelfY      = 0;  // top edge of the shelf being filled
        Int32 shelfHeight = 0;  // height of that shelf = its first (tallest) tile
        Int32 cursorX     = 0;

        for (Int32 i = 0; i < Count; i++)
        {
            const Int32 size = Resolutions[i];
            if (cursorX + size > width)
            {
                shelfY += shelfHeight;
                shelfHeight = 0;
                cursorX = 0;
            }

            OutRects[i].X    = cursorX;
            OutRects[i].Y    = shelfY;
            OutRects[i].Size = size;

            cursorX += size;
            shelfHeight = std::max(shelfHeight, size);
        }

        OutWidth  = width;
        OutHeight = shelfY + shelfHeight;
    }

    void ComputeCascadeSplits(const CascadeConfig& Config, float NearClip, DynamicArray<float>& OutSplits)
    {
        OutSplits.Clear();
        if (Config.CascadeCount <= 0) return;

        const float farClip = std::max(Config.ShadowDistance, NearClip + 1.0f);
        const float lambda  = glm::clamp(Config.SplitLambda, 0.0f, 1.0f);

        for (Int32 i = 1; i <= Config.CascadeCount; i++)
        {
            const float p = static_cast<float>(i) / static_cast<float>(Config.CascadeCount);

            // Logarithmic split — denser near the camera.
            const float logSplit = NearClip * std::pow(farClip / NearClip, p);
            // Uniform split — equal slices.
            const float uniformSplit = NearClip + (farClip - NearClip) * p;

            OutSplits.PushBack(logSplit * lambda + uniformSplit * (1.0f - lambda));
        }
    }

    void ComputeCascadeMatrices(
        const Matrix4& CameraView,
        float FovYRadians, float Aspect,
        float NearClip,
        const Vec3& LightDir,
        const CascadeConfig& Config,
        const DynamicArray<float>& Splits,
        DynamicArray<ShadowCascadeData>& OutCascades,
        const Int32* PerCascadeResolutions)
    {
        PLU_PROFILE_SCOPE("ComputeCascadeMatrices");

        OutCascades.Clear();
        if (Splits.IsEmpty()) return;

        const Vec3 nLightDir = glm::normalize(LightDir);
        const Vec3 up = (glm::abs(glm::dot(nLightDir, Vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
            ? Vec3(0.0f, 0.0f, 1.0f)
            : Vec3(0.0f, 1.0f, 0.0f);

        // FIXED light basis: the eye sits one unit "up-light" of the world origin and looks
        // along the light direction. Nothing here is derived from the camera, so light space
        // is the same every frame as long as the sun does not rotate — which is exactly what
        // makes the texel snap below a real stabiliser rather than a no-op. (The old code put
        // the eye at `center - lightDir * radius`, which maps `center` onto the light-space
        // origin, so flooring it always yielded 0 and the basis slid with the camera.)
        const Matrix4 lightView = glm::lookAt(-nLightDir, Vec3(0.0f), up);

        // Far margin only (behind the cascade sphere, away from the light). The near side needs
        // no margin: GL_DEPTH_CLAMP in the shadow pass flattens casters in front of the near
        // plane onto it, so they still occlude without stretching the depth range (and losing
        // precision) the way the old 50 m near margin did.
        constexpr float kZFarMargin = 1.0f;

        // Quantisation of the bounding-sphere radius, in metres. Rounding the radius up to a
        // fixed grid stops the ortho extent (and therefore the texel size) from changing by a
        // hair every frame, which would defeat the snap even with a fixed basis.
        constexpr float kRadiusQuantum = 1.0f / 16.0f;

        Vec3 corners[8];
        float prevSplit = NearClip;
        for (UInt32 i = 0; i < Splits.Size(); i++)
        {
            const float currSplit = Splits[i];

            const Int32 resolution = PerCascadeResolutions ? PerCascadeResolutions[i] : Config.Resolution;
            const float resolutionF = static_cast<float>(std::max(resolution, 1));

            // This cascade's sub-frustum: the camera's fov/aspect with its own near/far.
            const Matrix4 cascadeProj = GetCascadeProjectionMatrix(FovYRadians, Aspect, prevSplit, currSplit);
            GetFrustumCornersWorldSpace(cascadeProj, CameraView, corners);

            Vec3 center = Vec3(0.0f);
            for (const Vec3& c : corners)
                center += c;
            center /= 8.0f;

            // Bounding sphere of the sub-frustum. Independent of camera orientation (at a fixed
            // fov/near/far), so the shadow map extent does not pulse when the camera rotates.
            float radius = 0.0f;
            for (const Vec3& c : corners)
                radius = std::max(radius, glm::length(c - center));
            radius = std::ceil(radius / kRadiusQuantum) * kRadiusQuantum;

            const float texelWorldSize = (2.0f * radius) / resolutionF;

            // Texel snap inside the fixed light basis — the whole point of the fixed basis.
            Vec3 centerLS = Vec3(lightView * Vec4(center, 1.0f));
            centerLS.x = std::floor(centerLS.x / texelWorldSize) * texelWorldSize;
            centerLS.y = std::floor(centerLS.y / texelWorldSize) * texelWorldSize;

            // The light looks down -z, so a point at light-space z has depth -z. The sphere
            // spans [centerLS.z - radius, centerLS.z + radius] → near = -(z + r), far = -(z - r).
            const float zNear = -(centerLS.z + radius);
            const float zFar  = -(centerLS.z - radius) + kZFarMargin;
            const Matrix4 lightProj = glm::ortho(
                centerLS.x - radius, centerLS.x + radius,
                centerLS.y - radius, centerLS.y + radius,
                zNear, zFar);

            ShadowCascadeData data;
            data.ViewProj       = lightProj * lightView;
            data.View           = lightView;
            data.Proj           = lightProj;
            data.SplitDistance  = currSplit;
            data.TexelWorldSize = texelWorldSize;
            data.Radius         = radius;
            data.DepthRange     = zFar - zNear;
            data.Resolution     = resolution;
            OutCascades.PushBack(data);

            prevSplit = currSplit;
        }
    }

    void ComputeSpotBoundingSphere(const Vec3& Apex, const Vec3& Dir, float Range,
                                   float HalfAngleRadians, Vec3& OutCenter, float& OutRadius)
    {
        const Vec3 axis = glm::normalize(Dir);
        const float halfAngle = glm::clamp(HalfAngleRadians, 0.0f, glm::half_pi<float>());
        const float cosTheta = std::cos(halfAngle);

        // Narrow cone: the smallest enclosing sphere passes through the apex AND the base rim, so
        // its centre sits on the axis at Range / (2*cos²θ) with that same value as radius.
        // (At θ = 45° this degenerates into the wide-cone case, hence the >= comparison.)
        if (cosTheta * cosTheta >= 0.5f)
        {
            const float distance = Range / (2.0f * std::max(cosTheta * cosTheta, 1e-6f));
            OutCenter = Apex + axis * distance;
            OutRadius = distance;
            return;
        }

        // Wide cone: the base circle is the widest part, so the sphere is the one circumscribing
        // it — centred at the base, radius = the base's own radius.
        const float sinTheta = std::sin(halfAngle);
        OutCenter = Apex + axis * (Range * cosTheta);
        OutRadius = Range * sinTheta;
    }

    Matrix4 ComputeSpotLightMatrix(const Vec3& Apex, const Vec3& Dir, float Range, float OuterHalfAngleRadians)
    {
        Matrix4 view, proj;
        return ComputeSpotLightMatrix(Apex, Dir, Range, OuterHalfAngleRadians, view, proj);
    }

    Matrix4 ComputeSpotLightMatrix(const Vec3& Apex, const Vec3& Dir, float Range, float OuterHalfAngleRadians,
                                   Matrix4& OutView, Matrix4& OutProj)
    {
        const Vec3 axis = glm::normalize(Dir);

        // Pick the world axis least parallel to the light direction. A lamp pointing straight
        // down is the common case, and lookAt with an "up" parallel to its forward produces a
        // degenerate (all-NaN) basis.
        const Vec3 up = (glm::abs(glm::dot(axis, Vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
            ? Vec3(0.0f, 0.0f, 1.0f)
            : Vec3(0.0f, 1.0f, 0.0f);

        // Square projection covering the whole cone: the map is square, so the FOV is the full
        // (not half) outer angle in both axes. far = Range makes the light's falloff window and
        // its depth range the same distance, so no depth precision is spent past the last lit metre.
        const float fovY = glm::clamp(2.0f * OuterHalfAngleRadians, 0.01f, glm::pi<float>() - 0.01f);
        const float farPlane = std::max(Range, kSpotShadowNearClip + 0.01f);

        const Matrix4 lightProj = glm::perspective(fovY, 1.0f, kSpotShadowNearClip, farPlane);
        const Matrix4 lightView = glm::lookAt(Apex, Apex + axis * farPlane, up);
        OutView = lightView;
        OutProj = lightProj;
        return lightProj * lightView;
    }

    void AppendConeWireframe(DynamicArray<float>& OutLineVerts, const Vec3& Apex, const Vec3& Dir,
                             float Range, float HalfAngleRadians, const Vec3& Color, Int32 Segments,
                             float MaxHalfAngleRadians)
    {
        if (Range <= 0.0f || Segments < 3) return;

        const Vec3 axis = glm::normalize(Dir);
        const Vec3 reference = (glm::abs(glm::dot(axis, Vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
            ? Vec3(0.0f, 0.0f, 1.0f)
            : Vec3(0.0f, 1.0f, 0.0f);
        const Vec3 right = glm::normalize(glm::cross(axis, reference));
        const Vec3 up    = glm::cross(right, axis);

        const float halfAngle = glm::clamp(HalfAngleRadians, 0.0f, glm::clamp(MaxHalfAngleRadians, 0.0f, glm::pi<float>()));
        // The rim sits on the SPHERE of radius Range, not on a flat cap — that is where the
        // light actually stops, so the wireframe matches the falloff the shader computes.
        const Vec3  baseCenter = Apex + axis * (Range * std::cos(halfAngle));
        const float baseRadius = Range * std::sin(halfAngle);

        auto pushVertex = [&](const Vec3& position) {
            OutLineVerts.PushBack(position.x);
            OutLineVerts.PushBack(position.y);
            OutLineVerts.PushBack(position.z);
            OutLineVerts.PushBack(Color.r);
            OutLineVerts.PushBack(Color.g);
            OutLineVerts.PushBack(Color.b);
        };

        // 6 floats per vertex, 2 vertices per line: one rim segment plus one spoke each step.
        OutLineVerts.Reserve(OutLineVerts.Size() + static_cast<UInt32>(Segments) * 4 * 6);

        const float step = glm::two_pi<float>() / static_cast<float>(Segments);
        Vec3 previous = baseCenter + right * baseRadius;
        for (Int32 s = 1; s <= Segments; s++)
        {
            const float angle = step * static_cast<float>(s);
            const Vec3 current = baseCenter + (right * std::cos(angle) + up * std::sin(angle)) * baseRadius;

            pushVertex(previous);
            pushVertex(current);

            // Spokes from the apex — four of them is enough to read the cone as a cone without
            // turning the base into a solid disc of lines.
            if ((s % std::max(Segments / 4, 1)) == 0)
            {
                pushVertex(Apex);
                pushVertex(current);
            }

            previous = current;
        }
    }

    void AppendSphereWireframe(DynamicArray<float>& OutLineVerts, const Vec3& Center, float Radius,
                               const Vec3& Color, Int32 Segments)
    {
        if (Radius <= 0.0f || Segments < 3) return;

        auto pushVertex = [&](const Vec3& position) {
            OutLineVerts.PushBack(position.x);
            OutLineVerts.PushBack(position.y);
            OutLineVerts.PushBack(position.z);
            OutLineVerts.PushBack(Color.r);
            OutLineVerts.PushBack(Color.g);
            OutLineVerts.PushBack(Color.b);
        };

        // 3 circles, Segments lines each, 2 vertices of 6 floats per line.
        OutLineVerts.Reserve(OutLineVerts.Size() + static_cast<UInt32>(Segments) * 3 * 2 * 6);

        const Vec3 axes[3][2] = {
            {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)},
            {Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)},
            {Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)},
        };
        const float step = glm::two_pi<float>() / static_cast<float>(Segments);
        for (const auto& plane : axes)
        {
            Vec3 previous = Center + plane[0] * Radius;
            for (Int32 s = 1; s <= Segments; s++)
            {
                const float angle = step * static_cast<float>(s);
                const Vec3 current = Center + (plane[0] * std::cos(angle) + plane[1] * std::sin(angle)) * Radius;
                pushVertex(previous);
                pushVertex(current);
                previous = current;
            }
        }
    }

    Frustum ExtractFrustumPlanes(const Matrix4& viewProj)
    {
        // Gribb-Hartmann: każda płaszczyzna to kombinacja liniowa wierszy M, gdzie clip = M * pos.
        // Wewnątrz frustum: -w <= x <= w, -w <= y <= w, -w <= z <= w (konwencja NDC [-1,1] z glm::perspective/ortho).
        const Vec4 row0 = glm::row(viewProj, 0);
        const Vec4 row1 = glm::row(viewProj, 1);
        const Vec4 row2 = glm::row(viewProj, 2);
        const Vec4 row3 = glm::row(viewProj, 3);

        Frustum frustum;
        frustum.Planes[0] = row3 + row0; // Left
        frustum.Planes[1] = row3 - row0; // Right
        frustum.Planes[2] = row3 + row1; // Bottom
        frustum.Planes[3] = row3 - row1; // Top
        frustum.Planes[4] = row3 + row2; // Near
        frustum.Planes[5] = row3 - row2; // Far

        for (Vec4& plane : frustum.Planes) {
            const float length = glm::length(Vec3(plane));
            if (length > 0.0f) plane /= length;
        }
        return frustum;
    }

    bool SphereInFrustum(const Frustum& frustum, const Vec3& center, float radius)
    {
        for (const Vec4& plane : frustum.Planes) {
            if (glm::dot(Vec3(plane), center) + plane.w < -radius) return false;
        }
        return true;
    }

    bool SphereInFrustumNoNear(const Frustum& frustum, const Vec3& center, float radius)
    {
        // Index 4 is the near plane (see ExtractFrustumPlanes) — skipped on purpose, see the
        // declaration for why depth-clamped shadow casters live behind it.
        for (int i = 0; i < 6; i++) {
            if (i == 4) continue;
            const Vec4& plane = frustum.Planes[i];
            if (glm::dot(Vec3(plane), center) + plane.w < -radius) return false;
        }
        return true;
    }
}
