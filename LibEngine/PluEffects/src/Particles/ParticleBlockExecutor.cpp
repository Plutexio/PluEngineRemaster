//
// Created by Plutex on 9/26/26.
//

#include "PluEngine/Effects/Particles/ParticleBlockExecutor.h"
#include "PluEngine/Timer.h"
#include "PluEngine/Log.h"

#include <glm/gtc/quaternion.hpp>
#include <chrono>
#include <cmath>
#include <cstring>
#if defined(__AVX2__)
#include <immintrin.h>
#endif

namespace Plu
{
	namespace
	{
		constexpr float kPi = 3.14159265358979f;

		inline float NextRandom(UInt32& state)
		{
			// xorshift32; the top 24 bits give a uniform float in [0,1).
			state ^= state << 13;
			state ^= state >> 17;
			state ^= state << 5;
			return static_cast<float>(state >> 8) * (1.0f / 16777216.0f);
		}

		// An operand with everything the inner loop needs already pulled out of the program.
		struct Resolved
		{
			EParticleOperandKind Kind = EParticleOperandKind::Constant;
			float Scalar = 0.0f;
			const float* Column = nullptr;   // Attribute: the value; CurveOverAttribute: the curve's input
			const float* Lut = nullptr;
			UInt32 LutCount = 0;
			UInt32 Channels = 1;
			UInt32 Channel = 0;
			float InMin = 0.0f;
			float InvRange = 1.0f;
			float RandMin = 0.0f;
			float RandSpan = 0.0f;
		};

		struct ColumnSet
		{
			float* C[kParticleColumnCount] = {};
			float* operator[](EParticleColumn c) const { return C[static_cast<UInt32>(c)]; }
			float* At(UInt32 c) const { return C[c]; }
		};

		Resolved Resolve(const ParticleOperand& op, const CompiledEmitter& program, const ColumnSet& cols,
		                 const ParticleExecContext& ctx, UInt32 blockBegin)
		{
			Resolved r;
			r.Kind = op.Kind;
			switch (op.Kind) {
				case EParticleOperandKind::Constant:
					r.Scalar = op.Index < program.Constants.Size() ? program.Constants[op.Index] : 0.0f;
					break;
				case EParticleOperandKind::Parameter:
					if (ctx.ParameterValues && op.Index < ctx.ParameterValueCount) r.Scalar = ctx.ParameterValues[op.Index];
					else if (ctx.ParameterDefaults && op.Index < ctx.ParameterDefaultCount) r.Scalar = ctx.ParameterDefaults[op.Index];
					r.Kind = EParticleOperandKind::Constant; // from here on it is just a local float
					break;
				case EParticleOperandKind::Attribute:
					r.Column = cols.At(op.SrcAttribute);
					if (!r.Column) { r.Kind = EParticleOperandKind::Constant; r.Scalar = 0.0f; }
					else r.Column += blockBegin; // inner loops index from 0
					break;
				case EParticleOperandKind::CurveOverAttribute: {
					const float* source = cols.At(op.SrcAttribute);
					if (!source || op.Index >= program.Curves.Size()) { r.Kind = EParticleOperandKind::Constant; break; }
					const ParticleCurveLUT& lut = program.Curves[op.Index];
					r.Column = source + blockBegin;
					r.Lut = program.CurveSamples.Data() + lut.SampleOffset;
					r.LutCount = lut.SampleCount;
					r.Channels = lut.Channels;
					r.Channel = op.Channel;
					r.InMin = lut.InMin;
					const float range = lut.InMax - lut.InMin;
					r.InvRange = range > 1e-12f ? 1.0f / range : 0.0f;
					break;
				}
				case EParticleOperandKind::RandomRange:
					r.RandMin = op.Index < program.Constants.Size() ? program.Constants[op.Index] : 0.0f;
					r.RandSpan = (op.Index + 1 < program.Constants.Size() ? program.Constants[op.Index + 1] : r.RandMin) - r.RandMin;
					break;
			}
			return r;
		}

		inline float SampleLut(const Resolved& r, float x)
		{
			// Two loads and a lerp: no key search, no branch besides the clamp.
			float t = (x - r.InMin) * r.InvRange;
			t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
			const UInt32 lastSample = r.LutCount - 1;
			const float position = t * static_cast<float>(lastSample);
			UInt32 index = static_cast<UInt32>(position);
			if (index >= lastSample) index = lastSample > 0 ? lastSample - 1 : 0;
			const float frac = position - static_cast<float>(index);
			const float* s = r.Lut + index * r.Channels + r.Channel;
			const float next = lastSample > 0 ? s[r.Channels] : s[0];
			return s[0] + (next - s[0]) * frac;
		}

		// Calls body(fetch) with a `fetch(i)` specialised for the operand's kind, so the switch happens once
		// per op per block and each loop body is branch-free. `i` is relative to the block start.
		template <class Body>
		inline void WithOperand(const Resolved& r, UInt32& rng, Body&& body)
		{
			switch (r.Kind) {
				case EParticleOperandKind::Constant:
				case EParticleOperandKind::Parameter: {
					const float v = r.Scalar;
					body([v](UInt32) { return v; });
					break;
				}
				case EParticleOperandKind::Attribute: {
					const float* c = r.Column;
					body([c](UInt32 i) { return c[i]; });
					break;
				}
				case EParticleOperandKind::CurveOverAttribute: {
					const Resolved rr = r;
					body([rr](UInt32 i) { return SampleLut(rr, rr.Column[i]); });
					break;
				}
				case EParticleOperandKind::RandomRange: {
					UInt32* state = &rng;
					const float mn = r.RandMin, span = r.RandSpan;
					body([state, mn, span](UInt32) { return mn + span * NextRandom(*state); });
					break;
				}
			}
		}


		// dst[k][i] = base[k][i] * curve(source[i]).channel(k): one clamp/index computation shared by the N
		// columns it feeds (a colour gradient's four channels, or a size curve's two axes).
		template <int N>
		void MulCurveN(const Resolved& b, float* const* dst, const float* const* base, bool broadcast, UInt32 count)
		{
			// LUTs are baked with >= 2 samples, so `hi` is always a valid load and the loop has no branches
			// beyond the clamp (which compiles to min/max).
			const UInt32 lastSample = b.LutCount >= 2 ? b.LutCount - 1 : 1;
			const float scale = b.InvRange * static_cast<float>(lastSample);
			const float bias = -b.InMin * scale;
			const float maxPos = static_cast<float>(lastSample);
			const UInt32 stride = b.Channels;
			const UInt32 channelStep = broadcast ? 0 : 1;
			const float* lut = b.Lut + b.Channel;
			const float* source = b.Column;
			UInt32 i = 0;
#if defined(__AVX2__)
			// 8 particles per step: the LUT lookups are gathers, everything else is plain vector math.
			{
				const __m256 vScale = _mm256_set1_ps(scale), vBias = _mm256_set1_ps(bias);
				const __m256 vMax = _mm256_set1_ps(maxPos), vZero = _mm256_setzero_ps();
				const __m256i vLast = _mm256_set1_epi32(static_cast<int>(lastSample) - 1);
				const __m256i vStride = _mm256_set1_epi32(static_cast<int>(stride));
				for (; i + 8 <= count; i += 8) {
					__m256 position = _mm256_fmadd_ps(_mm256_loadu_ps(source + i), vScale, vBias);
					position = _mm256_min_ps(_mm256_max_ps(position, vZero), vMax);
					__m256i index = _mm256_min_epi32(_mm256_cvttps_epi32(position), vLast);
					const __m256 frac = _mm256_sub_ps(position, _mm256_cvtepi32_ps(index));
					const __m256i offset = _mm256_mullo_epi32(index, vStride);
					for (int k = 0; k < N; ++k) {
						const float* channelLut = lut + static_cast<UInt32>(k) * channelStep;
						const __m256 lo = _mm256_i32gather_ps(channelLut, offset, 4);
						const __m256 hi = _mm256_i32gather_ps(channelLut + stride, offset, 4);
						const __m256 scaleValue = _mm256_fmadd_ps(_mm256_sub_ps(hi, lo), frac, lo);
						_mm256_storeu_ps(dst[k] + i, _mm256_mul_ps(_mm256_loadu_ps(base[k] + i), scaleValue));
					}
				}
			}
#endif
			for (; i < count; ++i) {
				float position = source[i] * scale + bias;
				position = std::min(std::max(position, 0.0f), maxPos);
				const UInt32 index = std::min(static_cast<UInt32>(position), lastSample - 1);
				const float frac = position - static_cast<float>(index);
				const float* s = lut + index * stride;
				for (int k = 0; k < N; ++k) {
					const UInt32 channel = static_cast<UInt32>(k) * channelStep;
					const float lo = s[channel];
					const float hi = s[stride + channel];
					dst[k][i] = base[k][i] * (lo + (hi - lo) * frac);
				}
			}
		}

		Vec3 RandomUnitVector(UInt32& rng)
		{
			const float z = 2.0f * NextRandom(rng) - 1.0f;
			const float phi = 2.0f * kPi * NextRandom(rng);
			const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
			return Vec3(r * std::cos(phi), r * std::sin(phi), z);
		}

		// Uniform direction inside a cone of half-angle `coneDegrees` around `axis` (unit).
		Vec3 RandomInCone(const Vec3& axis, float coneDegrees, UInt32& rng)
		{
			if (coneDegrees <= 0.0f) return axis;
			const float cosMax = std::cos(std::min(coneDegrees, 180.0f) * kPi / 180.0f);
			const float cosTheta = 1.0f - NextRandom(rng) * (1.0f - cosMax);
			const float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
			const float phi = 2.0f * kPi * NextRandom(rng);
			const Vec3 helper = std::abs(axis.x) < 0.9f ? Vec3(1, 0, 0) : Vec3(0, 1, 0);
			const Vec3 u = glm::normalize(glm::cross(axis, helper));
			const Vec3 v = glm::cross(axis, u);
			return axis * cosTheta + (u * std::cos(phi) + v * std::sin(phi)) * sinTheta;
		}

		void ExecuteOp(const ParticleOp& op, const CompiledEmitter& program, const ColumnSet& cols,
		               ParticleExecContext& ctx, UInt32 begin, UInt32 end, const glm::mat3& rotation)
		{
			const UInt32 count = end - begin;
			if (count == 0) return;
			float* dst = cols.At(op.Dst);
			if (dst) dst += begin;
			const float dt = ctx.DeltaTime;
			UInt32& rng = ctx.RngState;

			switch (op.Code) {
				case EParticleOpCode::Set: {
					if (!dst) return;
					WithOperand(Resolve(op.A, program, cols, ctx, begin), rng, [&](auto&& f) {
						for (UInt32 i = 0; i < count; ++i) dst[i] = f(i);
					});
					break;
				}
				case EParticleOpCode::Add: {
					if (!dst) return;
					WithOperand(Resolve(op.A, program, cols, ctx, begin), rng, [&](auto&& f) {
						for (UInt32 i = 0; i < count; ++i) dst[i] += f(i);
					});
					break;
				}
				case EParticleOpCode::MulAddDt: {
					if (!dst) return;
					WithOperand(Resolve(op.A, program, cols, ctx, begin), rng, [&](auto&& f) {
						for (UInt32 i = 0; i < count; ++i) dst[i] += f(i) * dt;
					});
					break;
				}
				case EParticleOpCode::Damp: {
					if (!dst) return;
					const Resolved a = Resolve(op.A, program, cols, ctx, begin);
					if (a.Kind == EParticleOperandKind::Constant) {
						const float k = std::exp(-a.Scalar * dt); // one exp per block, not per particle
						for (UInt32 i = 0; i < count; ++i) dst[i] *= k;
					} else {
						WithOperand(a, rng, [&](auto&& f) {
							for (UInt32 i = 0; i < count; ++i) dst[i] *= std::exp(-f(i) * dt);
						});
					}
					break;
				}
				case EParticleOpCode::Mul: {
					if (!dst) return;
					const Resolved a = Resolve(op.A, program, cols, ctx, begin);
					const Resolved b = Resolve(op.B, program, cols, ctx, begin);
					WithOperand(a, rng, [&](auto&& fa) {
						WithOperand(b, rng, [&](auto&& fb) {
							for (UInt32 i = 0; i < count; ++i) dst[i] = fa(i) * fb(i);
						});
					});
					break;
				}
				case EParticleOpCode::MulCurve: {
					const UInt32 n = op.Flags & 0x7F;
					const bool broadcast = (op.Flags & 0x80) != 0;
					float* d[4]; const float* a[4];
					for (UInt32 k = 0; k < n && k < 4; ++k) {
						d[k] = cols.At(op.Dst + k);
						a[k] = cols.At(op.A.SrcAttribute + k);
						if (!d[k] || !a[k]) {
							// The compiler requires every fused column; reaching this is a compiler bug.
							static bool warned = false;
							if (!warned) {
								warned = true;
								PLU_CORE_WARN("Particle emitter '{}': MulCurve on column {} skipped, column {} not allocated",
								              program.Name.CStr(), static_cast<UInt32>(op.Dst), static_cast<UInt32>(!d[k] ? op.Dst + k : op.A.SrcAttribute + k));
							}
							return;
						}
						d[k] += begin; a[k] += begin;
					}
					const Resolved b = Resolve(op.B, program, cols, ctx, begin);
					if (b.Kind != EParticleOperandKind::CurveOverAttribute) {
						for (UInt32 k = 0; k < n && k < 4; ++k)
							for (UInt32 i = 0; i < count; ++i) d[k][i] = a[k][i] * b.Scalar;
						break;
					}
					switch (n) {
						case 1: MulCurveN<1>(b, d, a, broadcast, count); break;
						case 2: MulCurveN<2>(b, d, a, broadcast, count); break;
						case 3: MulCurveN<3>(b, d, a, broadcast, count); break;
						case 4: MulCurveN<4>(b, d, a, broadcast, count); break;
						default: break;
					}
					break;
				}
				case EParticleOpCode::Copy: {
					const float* src = cols.At(op.A.SrcAttribute);
					if (dst && src) std::memcpy(dst, src + begin, sizeof(float) * count);
					break;
				}
				case EParticleOpCode::AgeAdvance: {
					float* age = cols[EParticleColumn::Age] + begin;
					const float* life = cols[EParticleColumn::Lifetime] + begin;
					float* nage = cols[EParticleColumn::NormalizedAge] + begin;
					for (UInt32 i = 0; i < count; ++i) {
						age[i] += dt;
						const float n = age[i] / std::max(life[i], 1e-6f);
						nage[i] = n < 1.0f ? n : 1.0f;
					}
					break;
				}
				case EParticleOpCode::IntegratePosition: {
					for (UInt32 axis = 0; axis < 3; ++axis) {
						float* p = cols.At(static_cast<UInt32>(EParticleColumn::PosX) + axis) + begin;
						const float* v = cols.At(static_cast<UInt32>(EParticleColumn::VelX) + axis) + begin;
						for (UInt32 i = 0; i < count; ++i) p[i] += v[i] * dt;
					}
					break;
				}
				case EParticleOpCode::ComputeSpeed: {
					const float* vx = cols[EParticleColumn::VelX] + begin;
					const float* vy = cols[EParticleColumn::VelY] + begin;
					const float* vz = cols[EParticleColumn::VelZ] + begin;
					if (!dst) return;
					for (UInt32 i = 0; i < count; ++i) dst[i] = std::sqrt(vx[i] * vx[i] + vy[i] * vy[i] + vz[i] * vz[i]);
					break;
				}
				case EParticleOpCode::KillSlow: {
					const float* speed = cols[EParticleColumn::Speed];
					float* armed = cols[EParticleColumn::Custom0];
					float* life = cols[EParticleColumn::Lifetime];
					if (!speed || !armed) return;
					speed += begin; armed += begin; life += begin;
					const float threshold = Resolve(op.A, program, cols, ctx, begin).Scalar;
					for (UInt32 i = 0; i < count; ++i) {
						if (speed[i] > threshold) armed[i] = 1.0f;
						else if (armed[i] > 0.5f) life[i] = 0.0f; // death = zero lifetime; compaction removes it
					}
					break;
				}
				case EParticleOpCode::SubUV: {
					const float* p = program.Constants.Data() + op.A.Index; // fps, totalFrames
					const float fps = p[0], total = std::max(p[1], 1.0f);
					const float* age = cols[EParticleColumn::Age] + begin;
					const float* nage = cols[EParticleColumn::NormalizedAge] + begin;
					if (!dst) return;
					for (UInt32 i = 0; i < count; ++i) {
						float frame = fps > 0.0f ? std::floor(age[i] * fps) : std::floor(std::min(nage[i], 0.9999f) * total);
						dst[i] = std::fmod(frame, total);
					}
					break;
				}
				case EParticleOpCode::SpawnPosition: {
					const float* p = program.Constants.Data() + op.A.Index; // radius, ext xyz, coneAngle, offset xyz
					const float radius = p[0];
					const Vec3 extent(p[1], p[2], p[3]);
					const float cone = p[4];
					const Vec3 offset(p[5], p[6], p[7]);
					float* px = cols[EParticleColumn::PosX] + begin;
					float* py = cols[EParticleColumn::PosY] + begin;
					float* pz = cols[EParticleColumn::PosZ] + begin;
					for (UInt32 i = 0; i < count; ++i) {
						Vec3 local = offset;
						switch (static_cast<EParticleSpawnShape>(op.Flags)) {
							case EParticleSpawnShape::Point: break;
							case EParticleSpawnShape::Sphere:
								local += RandomUnitVector(rng) * (radius * std::cbrt(NextRandom(rng)));
								break;
							case EParticleSpawnShape::Box:
								local += Vec3(2.0f * NextRandom(rng) - 1.0f, 2.0f * NextRandom(rng) - 1.0f,
								              2.0f * NextRandom(rng) - 1.0f) * extent;
								break;
							case EParticleSpawnShape::Cone:
								// Solid spherical sector: apex at the spawner, axis forward (-Z), half angle `cone`,
								// reach `radius`. Uniform in volume — direction uniform on the cap, distance
								// radius * cbrt(u). 180 degrees is a full sphere, 0 a segment along the axis.
								local += RandomInCone(Vec3(0.0f, 0.0f, -1.0f), cone, rng) * (radius * std::cbrt(NextRandom(rng)));
								break;
						}
						const Vec3 world = ctx.Location + rotation * local;
						px[i] = world.x; py[i] = world.y; pz[i] = world.z;
					}
					break;
				}
				case EParticleOpCode::SpawnVelocity: {
					const float* p = program.Constants.Data() + op.B.Index; // dir xyz, coneAngle
					const Vec3 dir(p[0], p[1], p[2]);
					const float cone = p[3];
					float* vx = cols[EParticleColumn::VelX] + begin;
					float* vy = cols[EParticleColumn::VelY] + begin;
					float* vz = cols[EParticleColumn::VelZ] + begin;
					WithOperand(Resolve(op.A, program, cols, ctx, begin), rng, [&](auto&& speed) {
						for (UInt32 i = 0; i < count; ++i) {
							const Vec3 world = rotation * (RandomInCone(dir, cone, rng) * speed(i));
							vx[i] = world.x; vy[i] = world.y; vz[i] = world.z;
						}
					});
					break;
				}
				case EParticleOpCode::Count: break;
			}
		}

		void RunOps(ParticleExecContext& ctx, const DynamicArray<ParticleOp>& ops, DynamicArray<double>* timings,
		            UInt32 first, UInt32 last)
		{
			const CompiledEmitter& program = *ctx.Program;
			ColumnSet cols;
			for (UInt32 c = 0; c < kParticleColumnCount; ++c) cols.C[c] = ctx.Storage->Column(c);
			if (!cols[EParticleColumn::Age]) return;

			const glm::mat3 rotation = glm::mat3_cast(ctx.Rotation);
			if (timings) timings->Resize(ops.Size());

			// Blocks outermost, ops inside: a block's columns stay hot in cache across the whole op chain.
			for (UInt32 begin = first; begin < last; begin += kParticleBlockSize) {
				const UInt32 end = std::min(last, begin + kParticleBlockSize);
				for (UInt32 o = 0; o < ops.Size(); ++o) {
					if (timings) {
						const auto t0 = std::chrono::high_resolution_clock::now();
						ExecuteOp(ops[o], program, cols, ctx, begin, end, rotation);
						const auto t1 = std::chrono::high_resolution_clock::now();
						(*timings)[o] += std::chrono::duration<double, std::milli>(t1 - t0).count();
					} else {
						ExecuteOp(ops[o], program, cols, ctx, begin, end, rotation);
					}
				}
			}
		}
	}

	void ParticleBlockExecutor::RunSpawn(ParticleExecContext& ctx, UInt32 first, UInt32 last)
	{
		PLU_PROFILE_SCOPE("Particles/Spawn");
		if (!ctx.Program || !ctx.Storage || last <= first) return;
		RunOps(ctx, ctx.Program->SpawnOps, ctx.Timings ? &ctx.Timings->SpawnMs : nullptr, first, last);
	}

	void ParticleBlockExecutor::RunUpdate(ParticleExecContext& ctx)
	{
		PLU_PROFILE_SCOPE("Particles/Update");
		if (!ctx.Program || !ctx.Storage || ctx.Storage->Alive() == 0) return;
		RunOps(ctx, ctx.Program->UpdateOps, ctx.Timings ? &ctx.Timings->UpdateMs : nullptr, 0, ctx.Storage->Alive());
	}
}
