//
// Created by Plutex on 10/7/26.
//

#ifndef PLUENGINE_DEBUGDRAWADAPTER_H
#define PLUENGINE_DEBUGDRAWADAPTER_H

#include "PluEngine/Core.h"
#include "PluEngine/PluTypes.h"
#include "Array/Array.h"
#include "PluEngine/Gameplay/RaycastInfo.h"
#include "DebugDrawAdapter.generated.h"

namespace Plu
{
    // Geometry of draws that outlive their frame, already in the line/point buffer format. Not
    // reflected — internal to DebugDrawAdapter.
    struct DebugDrawPersistentBatch
    {
        float RemainingSeconds = 0.0f;
        // Set once the batch has been emitted. Until then, further draws with the same duration
        // join it instead of opening a batch of their own.
        bool Sealed = false;
        DynamicArray<float> LineVerts;
        DynamicArray<float> PointVerts;
    };

    // Front end for every debug draw of one SceneWorld (SceneWorld::GetDebugDraw). Writes into the
    // world's per-frame debug buffers — interleaved pos(3)+color(3), GL_LINES / GL_POINTS — which
    // RenderSnapshotBuilder drains into the render snapshot each frame. Main thread only, like the
    // world itself.
    //
    // Units: positions and sizes in metres, rotations as engine Euler angles in degrees, cone angles
    // in degrees. Duration in seconds: 0 draws for this frame only, anything above keeps the shape
    // on screen for that long (counted in frame time by the snapshot builder).
    PLU_STRUCT(PyExport)
    struct PLUGAMEPLAY_API DebugDrawAdapter
    {
        REFLECTION_BODY_DEBUGDRAWADAPTER()
    public:
        DebugDrawAdapter() = default;
        DebugDrawAdapter(DynamicArray<float>* lineVerts, DynamicArray<float>* pointVerts);

        PLU_FUNCTION(PyExport)
        void DrawDebugLine(const Vec3& Start, const Vec3& End, const Vec3& Color, float Duration = 0.0f);
        PLU_FUNCTION(PyExport)
        void DrawDebugPoint(const Vec3& Point, const Vec3& Color, float Duration = 0.0f);
        // Line with a four-spoke head at End. HeadSize is the head's length in metres.
        PLU_FUNCTION(PyExport)
        void DrawDebugArrow(const Vec3& Start, const Vec3& End, const Vec3& Color, float HeadSize = 0.2f, float Duration = 0.0f);

        // Axis-aligned box from its centre and half extent.
        PLU_FUNCTION(PyExport)
        void DrawDebugBox(const Vec3& Center, const Vec3& HalfExtent, const Vec3& Color, float Duration = 0.0f);
        // Box rotated by Rotation (Euler degrees) around its centre.
        PLU_FUNCTION(PyExport)
        void DrawDebugOrientedBox(const Vec3& Center, const Vec3& HalfExtent, const Vec3& Rotation, const Vec3& Color, float Duration = 0.0f);
        // Axis-aligned box from its min/max corners (bounds, AABBs).
        PLU_FUNCTION(PyExport)
        void DrawDebugBounds(const Vec3& Min, const Vec3& Max, const Vec3& Color, float Duration = 0.0f);

        // Three axis-aligned great circles.
        PLU_FUNCTION(PyExport)
        void DrawDebugSphere(const Vec3& Center, float Radius, const Vec3& Color, float Duration = 0.0f, Int32 Segments = 32);
        // Circle in the plane perpendicular to Normal.
        PLU_FUNCTION(PyExport)
        void DrawDebugCircle(const Vec3& Center, const Vec3& Normal, float Radius, const Vec3& Color, float Duration = 0.0f, Int32 Segments = 32);
        // Cone from Apex along Direction. The rim lies on the sphere of radius Length (where a spot
        // light actually ends), and HalfAngleDegrees may go up to 180 — past 90 it opens backwards.
        PLU_FUNCTION(PyExport)
        void DrawDebugCone(const Vec3& Apex, const Vec3& Direction, float Length, float HalfAngleDegrees, const Vec3& Color, float Duration = 0.0f, Int32 Segments = 24);
        // Cylinder between the centres of its two caps.
        PLU_FUNCTION(PyExport)
        void DrawDebugCylinder(const Vec3& Start, const Vec3& End, float Radius, const Vec3& Color, float Duration = 0.0f, Int32 Segments = 24);
        // Capsule along its local Y axis (rotated by Rotation, Euler degrees). HalfHeight is half the
        // length of the cylindrical part, as in Jolt — the full height is 2 * (HalfHeight + Radius).
        PLU_FUNCTION(PyExport)
        void DrawDebugCapsule(const Vec3& Center, float HalfHeight, float Radius, const Vec3& Rotation, const Vec3& Color, float Duration = 0.0f, Int32 Segments = 24);
        // Local X/Y/Z axes of a transform as red/green/blue lines, Size metres long.
        PLU_FUNCTION(PyExport)
        void DrawDebugAxes(const Vec3& Location, const Vec3& Rotation, float Size = 1.0f, float Duration = 0.0f);
        // A raycast result: green up to the hit, red past it (all red on a miss), the hit point and
        // its normal in yellow.
        PLU_FUNCTION(PyExport)
        void DrawDebugRaycast(const RaycastHitInfo& Hit, float Duration = 0.0f);

        // Drops every draw that is still waiting out its duration.
        PLU_FUNCTION(PyExport)
        void ClearPersistentDraws();

        // Appends the draws that are still alive to this frame's buffers and ages them by DeltaTime.
        // Called once per frame by RenderSnapshotBuilder, right before it drains the buffers.
        void EmitPersistentDraws(float DeltaTime);

        // Raw per-frame buffers, for code that packs a whole batch at once (PhysicsWorld's
        // wireframe/point renderers). Same format and lifetime as everything drawn above.
        [[nodiscard]] DynamicArray<float>* GetFrameLineBuffer() const { return mLineVerts; }
        [[nodiscard]] DynamicArray<float>* GetFramePointBuffer() const { return mPointVerts; }

    private:
        // Where a draw of the given duration goes: this frame's buffer, or a persistent batch.
        // Null when the adapter is not bound to a world (default-constructed).
        DynamicArray<float>* LineTarget(float Duration);
        DynamicArray<float>* PointTarget(float Duration);
        DebugDrawPersistentBatch& OpenPersistentBatch(float Duration);

        static void PushVertex(DynamicArray<float>& Out, const Vec3& Position, const Vec3& Color);
        static void PushLine(DynamicArray<float>& Out, const Vec3& A, const Vec3& B, const Vec3& Color);
        static void PushCircle(DynamicArray<float>& Out, const Vec3& Center, const Vec3& AxisA, const Vec3& AxisB, float Radius, const Vec3& Color, Int32 Segments);
        // Half circle from +AxisA through +AxisB to -AxisA.
        static void PushArc(DynamicArray<float>& Out, const Vec3& Center, const Vec3& AxisA, const Vec3& AxisB, float Radius, const Vec3& Color, Int32 Segments);
        static void PushBox(DynamicArray<float>& Out, const Vec3& Center, const glm::mat3& Basis, const Vec3& HalfExtent, const Vec3& Color);

        DynamicArray<float>* mLineVerts = nullptr;
        DynamicArray<float>* mPointVerts = nullptr;
        DynamicArray<DebugDrawPersistentBatch> mPersistentBatches;
    };
}

#endif //PLUENGINE_DEBUGDRAWADAPTER_H
