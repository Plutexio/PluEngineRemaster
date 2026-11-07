//
// Created by Plutex on 10/7/26.
//

#include "PluEngine/Gameplay/Debug/DebugDrawAdapter.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Render/RenderUtils.h"
#include "PluEngine/Timer.h"
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace
{
    const Vec3 kRaycastTraceColor = Vec3(0.2f, 1.0f, 0.2f);
    const Vec3 kRaycastPastHitColor = Vec3(1.0f, 0.2f, 0.2f);
    const Vec3 kRaycastHitColor = Vec3(1.0f, 0.9f, 0.1f);
    constexpr float kRaycastNormalLength = 0.25f;

    // Two unit vectors perpendicular to Axis and to each other. The reference axis is swapped when
    // Axis is (nearly) parallel to Y, so the cross products never degenerate.
    void MakePerpendicularBasis(const Vec3& Axis, Vec3& OutRight, Vec3& OutUp)
    {
        const Vec3 reference = (glm::abs(glm::dot(Axis, Vec3(0.0f, 1.0f, 0.0f))) > 0.99f)
            ? Vec3(0.0f, 0.0f, 1.0f)
            : Vec3(0.0f, 1.0f, 0.0f);
        OutRight = glm::normalize(glm::cross(Axis, reference));
        OutUp = glm::cross(OutRight, Axis);
    }

    glm::mat3 RotationBasis(const Vec3& RotationDegrees)
    {
        return glm::mat3_cast(Plu::GetQuaternionFromEuler(RotationDegrees));
    }
}

Plu::DebugDrawAdapter::DebugDrawAdapter(DynamicArray<float>* lineVerts, DynamicArray<float>* pointVerts)
    : mLineVerts(lineVerts), mPointVerts(pointVerts)
{
}

void Plu::DebugDrawAdapter::DrawDebugLine(const Vec3& Start, const Vec3& End, const Vec3& Color, float Duration)
{
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;
    PushLine(*out, Start, End, Color);
}

void Plu::DebugDrawAdapter::DrawDebugPoint(const Vec3& Point, const Vec3& Color, float Duration)
{
    DynamicArray<float>* out = PointTarget(Duration);
    if (!out) return;
    PushVertex(*out, Point, Color);
}

void Plu::DebugDrawAdapter::DrawDebugArrow(const Vec3& Start, const Vec3& End, const Vec3& Color, float HeadSize, float Duration)
{
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;
    PushLine(*out, Start, End, Color);

    const Vec3 shaft = End - Start;
    const float length = glm::length(shaft);
    if (length < 1e-6f || HeadSize <= 0.0f) return;

    const Vec3 direction = shaft / length;
    Vec3 right, up;
    MakePerpendicularBasis(direction, right, up);
    // The head never gets longer than the arrow itself.
    const float headLength = glm::min(HeadSize, length);
    const Vec3 headBase = End - direction * headLength;
    const float headRadius = headLength * 0.4f;
    for (const Vec3& spoke : {right, -right, up, -up}) {
        PushLine(*out, End, headBase + spoke * headRadius, Color);
    }
}

void Plu::DebugDrawAdapter::DrawDebugBox(const Vec3& Center, const Vec3& HalfExtent, const Vec3& Color, float Duration)
{
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;
    PushBox(*out, Center, glm::mat3(1.0f), HalfExtent, Color);
}

void Plu::DebugDrawAdapter::DrawDebugOrientedBox(const Vec3& Center, const Vec3& HalfExtent, const Vec3& Rotation, const Vec3& Color, float Duration)
{
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;
    PushBox(*out, Center, RotationBasis(Rotation), HalfExtent, Color);
}

void Plu::DebugDrawAdapter::DrawDebugBounds(const Vec3& Min, const Vec3& Max, const Vec3& Color, float Duration)
{
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;
    PushBox(*out, (Min + Max) * 0.5f, glm::mat3(1.0f), (Max - Min) * 0.5f, Color);
}

void Plu::DebugDrawAdapter::DrawDebugSphere(const Vec3& Center, float Radius, const Vec3& Color, float Duration, Int32 Segments)
{
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;
    AppendSphereWireframe(*out, Center, Radius, Color, Segments);
}

void Plu::DebugDrawAdapter::DrawDebugCircle(const Vec3& Center, const Vec3& Normal, float Radius, const Vec3& Color, float Duration, Int32 Segments)
{
    if (glm::length(Normal) < 1e-6f) return;
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;
    Vec3 right, up;
    MakePerpendicularBasis(glm::normalize(Normal), right, up);
    PushCircle(*out, Center, right, up, Radius, Color, Segments);
}

void Plu::DebugDrawAdapter::DrawDebugCone(const Vec3& Apex, const Vec3& Direction, float Length, float HalfAngleDegrees, const Vec3& Color, float Duration, Int32 Segments)
{
    if (glm::length(Direction) < 1e-6f) return;
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;
    AppendConeWireframe(*out, Apex, Direction, Length, glm::radians(glm::clamp(HalfAngleDegrees, 0.0f, 180.0f)),
                        Color, Segments, glm::pi<float>());
}

void Plu::DebugDrawAdapter::DrawDebugCylinder(const Vec3& Start, const Vec3& End, float Radius, const Vec3& Color, float Duration, Int32 Segments)
{
    const Vec3 axis = End - Start;
    if (glm::length(axis) < 1e-6f || Radius <= 0.0f || Segments < 3) return;
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;

    Vec3 right, up;
    MakePerpendicularBasis(glm::normalize(axis), right, up);
    PushCircle(*out, Start, right, up, Radius, Color, Segments);
    PushCircle(*out, End, right, up, Radius, Color, Segments);
    for (const Vec3& side : {right, -right, up, -up}) {
        PushLine(*out, Start + side * Radius, End + side * Radius, Color);
    }
}

void Plu::DebugDrawAdapter::DrawDebugCapsule(const Vec3& Center, float HalfHeight, float Radius, const Vec3& Rotation, const Vec3& Color, float Duration, Int32 Segments)
{
    if (Radius <= 0.0f || Segments < 3) return;
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;

    const glm::mat3 basis = RotationBasis(Rotation);
    const Vec3 right = basis[0];
    const Vec3 axis = basis[1];
    const Vec3 forward = basis[2];
    const float halfHeight = glm::max(HalfHeight, 0.0f);
    const Vec3 top = Center + axis * halfHeight;
    const Vec3 bottom = Center - axis * halfHeight;

    PushCircle(*out, top, right, forward, Radius, Color, Segments);
    PushCircle(*out, bottom, right, forward, Radius, Color, Segments);
    for (const Vec3& side : {right, -right, forward, -forward}) {
        PushLine(*out, top + side * Radius, bottom + side * Radius, Color);
    }
    // Hemispherical caps as two crossing arcs each.
    const Int32 arcSegments = glm::max(Segments / 2, 2);
    PushArc(*out, top, right, axis, Radius, Color, arcSegments);
    PushArc(*out, top, forward, axis, Radius, Color, arcSegments);
    PushArc(*out, bottom, right, -axis, Radius, Color, arcSegments);
    PushArc(*out, bottom, forward, -axis, Radius, Color, arcSegments);
}

void Plu::DebugDrawAdapter::DrawDebugAxes(const Vec3& Location, const Vec3& Rotation, float Size, float Duration)
{
    DynamicArray<float>* out = LineTarget(Duration);
    if (!out) return;
    const glm::mat3 basis = RotationBasis(Rotation);
    PushLine(*out, Location, Location + basis[0] * Size, Vec3(1.0f, 0.0f, 0.0f));
    PushLine(*out, Location, Location + basis[1] * Size, Vec3(0.0f, 1.0f, 0.0f));
    PushLine(*out, Location, Location + basis[2] * Size, Vec3(0.0f, 0.0f, 1.0f));
}

void Plu::DebugDrawAdapter::DrawDebugRaycast(const RaycastHitInfo& Hit, float Duration)
{
    if (!Hit.Hit) {
        DrawDebugLine(Hit.TraceStart, Hit.TraceEnd, kRaycastPastHitColor, Duration);
        return;
    }
    DrawDebugLine(Hit.TraceStart, Hit.HitLocation, kRaycastTraceColor, Duration);
    DrawDebugLine(Hit.HitLocation, Hit.TraceEnd, kRaycastPastHitColor, Duration);
    DrawDebugPoint(Hit.HitLocation, kRaycastHitColor, Duration);
    DrawDebugLine(Hit.HitLocation, Hit.HitLocation + Hit.HitNormal * kRaycastNormalLength, kRaycastHitColor, Duration);
}

void Plu::DebugDrawAdapter::ClearPersistentDraws()
{
    mPersistentBatches.Clear();
}

void Plu::DebugDrawAdapter::EmitPersistentDraws(float DeltaTime)
{
    if (mPersistentBatches.IsEmpty()) return;
    PLU_PROFILE_SCOPE("DebugDraw EmitPersistent");

    for (DebugDrawPersistentBatch& batch : mPersistentBatches) {
        if (mLineVerts && !batch.LineVerts.IsEmpty()) mLineVerts->Append(batch.LineVerts);
        if (mPointVerts && !batch.PointVerts.IsEmpty()) mPointVerts->Append(batch.PointVerts);
        batch.Sealed = true;
        batch.RemainingSeconds -= DeltaTime;
    }
    mPersistentBatches.RemoveIf([](const DebugDrawPersistentBatch& batch) {
        return batch.RemainingSeconds <= 0.0f;
    });
}

DynamicArray<float>* Plu::DebugDrawAdapter::LineTarget(float Duration)
{
    if (!mLineVerts) return nullptr;
    if (Duration <= 0.0f) return mLineVerts;
    return &OpenPersistentBatch(Duration).LineVerts;
}

DynamicArray<float>* Plu::DebugDrawAdapter::PointTarget(float Duration)
{
    if (!mPointVerts) return nullptr;
    if (Duration <= 0.0f) return mPointVerts;
    return &OpenPersistentBatch(Duration).PointVerts;
}

Plu::DebugDrawPersistentBatch& Plu::DebugDrawAdapter::OpenPersistentBatch(float Duration)
{
    // Scripts tend to draw several shapes with the same duration in one frame (a raycast is four
    // draws already); they share one batch instead of allocating a pair of arrays each.
    if (!mPersistentBatches.IsEmpty()) {
        DebugDrawPersistentBatch& last = mPersistentBatches.Back();
        if (!last.Sealed && last.RemainingSeconds == Duration) return last;
    }
    DebugDrawPersistentBatch batch;
    batch.RemainingSeconds = Duration;
    mPersistentBatches.PushBack(std::move(batch));
    return mPersistentBatches.Back();
}

void Plu::DebugDrawAdapter::PushVertex(DynamicArray<float>& Out, const Vec3& Position, const Vec3& Color)
{
    Out.PushBack(Position.x);
    Out.PushBack(Position.y);
    Out.PushBack(Position.z);
    Out.PushBack(Color.r);
    Out.PushBack(Color.g);
    Out.PushBack(Color.b);
}

void Plu::DebugDrawAdapter::PushLine(DynamicArray<float>& Out, const Vec3& A, const Vec3& B, const Vec3& Color)
{
    PushVertex(Out, A, Color);
    PushVertex(Out, B, Color);
}

void Plu::DebugDrawAdapter::PushCircle(DynamicArray<float>& Out, const Vec3& Center, const Vec3& AxisA, const Vec3& AxisB, float Radius, const Vec3& Color, Int32 Segments)
{
    if (Radius <= 0.0f || Segments < 3) return;
    const float step = glm::two_pi<float>() / static_cast<float>(Segments);
    Vec3 previous = Center + AxisA * Radius;
    for (Int32 s = 1; s <= Segments; s++) {
        const float angle = step * static_cast<float>(s);
        const Vec3 current = Center + (AxisA * std::cos(angle) + AxisB * std::sin(angle)) * Radius;
        PushLine(Out, previous, current, Color);
        previous = current;
    }
}

void Plu::DebugDrawAdapter::PushArc(DynamicArray<float>& Out, const Vec3& Center, const Vec3& AxisA, const Vec3& AxisB, float Radius, const Vec3& Color, Int32 Segments)
{
    const float step = glm::pi<float>() / static_cast<float>(Segments);
    Vec3 previous = Center + AxisA * Radius;
    for (Int32 s = 1; s <= Segments; s++) {
        const float angle = step * static_cast<float>(s);
        const Vec3 current = Center + (AxisA * std::cos(angle) + AxisB * std::sin(angle)) * Radius;
        PushLine(Out, previous, current, Color);
        previous = current;
    }
}

void Plu::DebugDrawAdapter::PushBox(DynamicArray<float>& Out, const Vec3& Center, const glm::mat3& Basis, const Vec3& HalfExtent, const Vec3& Color)
{
    Vec3 corners[8];
    for (int i = 0; i < 8; ++i) {
        const Vec3 sign((i & 1) ? 1.0f : -1.0f, (i & 2) ? 1.0f : -1.0f, (i & 4) ? 1.0f : -1.0f);
        corners[i] = Center + Basis * (sign * HalfExtent);
    }
    // Corners differ in one bit per edge: bit 0 = X, bit 1 = Y, bit 2 = Z.
    constexpr int edges[12][2] = { {0, 1}, {2, 3}, {4, 5}, {6, 7}, {0, 2}, {1, 3}, {4, 6}, {5, 7}, {0, 4}, {1, 5}, {2, 6}, {3, 7} };
    Out.Reserve(Out.Size() + 12 * 2 * 6);
    for (const auto& edge : edges) PushLine(Out, corners[edge[0]], corners[edge[1]], Color);
}
