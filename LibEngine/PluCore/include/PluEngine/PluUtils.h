//
// Created by Plutex on 1/14/26.
//

#ifndef PLUENGINE_PLUUTILS_H
#define PLUENGINE_PLUUTILS_H

#include "Core.h"
#include "PluSTL/PluSTL_FWD.h"
#include "PluTypes.h"
#include <cmath>

namespace Plu
{
	PLUCORE_API PathW GetEngineResourcesDir();

	PLUCORE_API PathW GetExePath();

	PLUCORE_API Path GetSystemUserPath();


	PLU_FUNCTION()
	PLUCORE_API Vec3 GetForwardVector(Vec3 rot);

	PLU_FUNCTION()
	PLUCORE_API Vec3 GetRightVector(Vec3 rot);

	PLU_FUNCTION()
	PLUCORE_API Vec3 GetUpVector(Vec3 rot);

	PLU_FUNCTION()
	PLUCORE_API double ClampD(double val, double min, double max);
	PLU_FUNCTION()
	PLUCORE_API float ClampF(float val, float min, float max);
	PLU_FUNCTION()
	PLUCORE_API int ClampI(int val, int min, int max);
	PLU_FUNCTION()
	PLUCORE_API float ClampAngle(float angle, float min, float max);

	// Wraps an angle in degrees into (-180, 180] — the form in which a difference between two
	// angles is the shortest way around, not the long one.
	PLU_FUNCTION()
	PLUCORE_API float NormalizeAxisDegrees(float angle);

	// --- Linear interpolation --------------------------------------------------------------
	// Returns val at alpha=0 and target at alpha=1. Alpha is not clamped, so values outside
	// [0,1] extrapolate. LerpI rounds the result to the nearest integer.
	PLU_FUNCTION()
	PLUCORE_API double LerpD(double val, double target, double alpha);
	PLU_FUNCTION()
	PLUCORE_API float LerpF(float val, float target, float alpha);
	PLU_FUNCTION()
	PLUCORE_API int LerpI(int val, int target, float alpha);
	PLU_FUNCTION()
	PLUCORE_API Vec3 LerpVec3(Vec3 val, Vec3 target, float alpha);

	// Same as Lerp*, but alpha is clamped to [0,1] first — the result never leaves the
	// val..target range.
	PLU_FUNCTION()
	PLUCORE_API double LerpClampedD(double val, double target, double alpha);
	PLU_FUNCTION()
	PLUCORE_API float LerpClampedF(float val, float target, float alpha);
	PLU_FUNCTION()
	PLUCORE_API int LerpClampedI(int val, int target, float alpha);
	PLU_FUNCTION()
	PLUCORE_API Vec3 LerpClampedVec3(Vec3 val, Vec3 target, float alpha);

	// --- Eased interpolation towards a target (per frame) ----------------------------------
	// Unlike Lerp*, these take the frame delta instead of an alpha: each call closes a
	// clamp(deltaTime * interpSpeed, 0, 1) fraction of the remaining distance, so the motion
	// eases out as it approaches and never overshoots. Feed the result back into the same
	// variable every tick:
	//     Location = Plu::InterpToVec3(Location, TargetLocation, deltaTime, 5.0f);
	// interpSpeed is "how aggressively to chase" (1/s), not a velocity — roughly, the value
	// covers ~63% of the distance in 1/interpSpeed seconds. interpSpeed <= 0 snaps straight to
	// target (no smoothing), and a value close enough to target also snaps, so the chase does
	// actually finish. Note this is frame-rate dependent by design (same as Unreal): a longer
	// frame moves further, and once deltaTime * interpSpeed >= 1 it is a plain snap.
	PLU_FUNCTION()
	PLUCORE_API double InterpToD(double val, double target, double deltaTime, double interpSpeed);
	PLU_FUNCTION()
	PLUCORE_API float InterpToF(float val, float target, float deltaTime, float interpSpeed);
	PLU_FUNCTION()
	PLUCORE_API Vec3 InterpToVec3(Vec3 val, Vec3 target, float deltaTime, float interpSpeed);

	// Rotation variant — Vec3 of Euler degrees (pitch=X, yaw=Y, roll=Z), like every other
	// rotation in the engine. Each axis is interpolated the shortest way around (350° -> 10°
	// goes through 0, not backwards through 180), and the result is normalized to (-180, 180].
	PLU_FUNCTION()
	PLUCORE_API Vec3 InterpToRotator(Vec3 val, Vec3 target, float deltaTime, float interpSpeed);

	// Quaternion variant — same easing, applied as a shortest-arc slerp. Prefer this over
	// InterpToRotator wherever rotations are already quaternions (animation pipeline), it has
	// no gimbal/wrap-around caveats. Not reflected (quaternions are not exposed to Python).
	PLUCORE_API Quaternion InterpToQuat(const Quaternion& val, const Quaternion& target, float deltaTime, float interpSpeed);

	// --- Constant-speed interpolation towards a target (per frame) -------------------------
	// Same call shape as InterpTo*, but the step is a fixed interpSpeed * deltaTime — units per
	// second (degrees per second for the rotator), i.e. an actual velocity. No ease-out: the
	// value moves at a steady rate and lands exactly on target instead of creeping towards it.
	// Never overshoots. Use these for things that should move at a known rate (a turret slewing
	// at 90°/s), and InterpTo* for camera/aim-style "smoothly catch up" motion.
	PLU_FUNCTION()
	PLUCORE_API double InterpConstantToD(double val, double target, double deltaTime, double interpSpeed);
	PLU_FUNCTION()
	PLUCORE_API float InterpConstantToF(float val, float target, float deltaTime, float interpSpeed);
	PLU_FUNCTION()
	PLUCORE_API Vec3 InterpConstantToVec3(Vec3 val, Vec3 target, float deltaTime, float interpSpeed);
	PLU_FUNCTION()
	PLUCORE_API Vec3 InterpConstantToRotator(Vec3 val, Vec3 target, float deltaTime, float interpSpeed);

	PLUCORE_API Vec3 GetLookAtRotatorDegrees(const Vec3& eye, const Vec3& target);
	PLUCORE_API Vec3 GetRotatedPointWithRadius(const Vec3& center, float radius, float angleDegrees, const Vec3& axis);
	PLUCORE_API Vec3 GetSphericalOrbitPoint(const Vec3& center, float radius, float yawDegrees, float pitchDegrees);

	PLUCORE_API void NormalizeVec3Rotation(Vec3* vec);

	// --- Decompose a transform matrix into its components ---------------------------------
	// Location is the translation column, scale is the length of each basis vector, and
	// rotation is returned as Euler angles in degrees (pitch=X, yaw=Y, roll=Z), matching the
	// rotation convention used everywhere else in the engine.
	PLUCORE_API Vec3 GetLocationFromMatrix(const Matrix4& matrix);
	PLUCORE_API Vec3 GetScaleFromMatrix(const Matrix4& matrix);
	PLUCORE_API Vec3 GetRotationFromMatrix(const Matrix4& matrix);

	PLUCORE_API Quaternion GetQuaternionFromEuler(Vec3 angles);

	// --- Picking: UInt32 ID <-> color ------------------------------------------------------
	// Pack a 32-bit id (e.g. a truncated UUID / per-frame object index) into an RGBA color so it
	// can be written to a picking framebuffer and read back. Each byte of the id maps to one
	// channel, normalized to [0,1] for a regular RGBA8 target:
	//   R = bits  0..7, G = bits  8..15, B = bits 16..23, A = bits 24..31
	// Inverse is UnpackColorToUInt32; rounding makes the round-trip exact for 8-bit channels.
	inline Vec4 PackUInt32ToColor(UInt32 id)
	{
		return Vec4(
			static_cast<float>( id        & 0xFFu) / 255.0f,
			static_cast<float>((id >>  8u) & 0xFFu) / 255.0f,
			static_cast<float>((id >> 16u) & 0xFFu) / 255.0f,
			static_cast<float>((id >> 24u) & 0xFFu) / 255.0f);
	}

	inline UInt32 UnpackColorToUInt32(const Vec4& color)
	{
		const UInt32 r = static_cast<UInt32>(std::lround(color.r * 255.0f)) & 0xFFu;
		const UInt32 g = static_cast<UInt32>(std::lround(color.g * 255.0f)) & 0xFFu;
		const UInt32 b = static_cast<UInt32>(std::lround(color.b * 255.0f)) & 0xFFu;
		const UInt32 a = static_cast<UInt32>(std::lround(color.a * 255.0f)) & 0xFFu;
		return r | (g << 8u) | (b << 16u) | (a << 24u);
	}

	// --- Per-thread frame timing ----------------------------------------------------------
	// The Main thread (game/UI loop) and the Render thread run independently (decoupled via the
	// RenderSnapshot triple buffer), so they tick at different rates. Each loop publishes its
	// last frame delta through these setters; the getters return the corresponding FPS.
	// All four are thread-safe (backed by atomics).
	PLUCORE_API void SetMainThreadDeltaTime(float deltaSeconds);
	PLUCORE_API void SetRenderThreadDeltaTime(float deltaSeconds);

	PLU_FUNCTION()
	PLUCORE_API float GetMainThreadFPS();
	PLU_FUNCTION()
	PLUCORE_API float GetRenderThreadFPS();

	// --- Render frame stats (draw calls / instances / culled) ------------------------------
	// Renderer::RenderSnapshot (render thread) tallies these while actually drawing (so they
	// reflect batching/culling decisions made there) and publishes the final per-frame values
	// here. Editor panels (main thread) read them the same way they read the FPS counters above
	// — a live RenderSnapshot object isn't safe to read cross-thread, but these mirrors are.
	PLUCORE_API void SetRenderFrameStats(UInt32 drawCalls, UInt32 instancesDrawn, UInt32 culledCount);

	PLU_FUNCTION()
	PLUCORE_API UInt32 GetStatDrawCalls();
	PLU_FUNCTION()
	PLUCORE_API UInt32 GetStatInstancesDrawn();
	PLU_FUNCTION()
	PLUCORE_API UInt32 GetStatCulledCount();


	PLUCORE_API String MakeStringForDisplay(String text);
	PLUCORE_API String PrepareCodeForDistribution(String code);
}

#endif //PLUENGINE_PLUUTILS_H