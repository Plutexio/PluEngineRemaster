//
// Created by Plutex on 2026-02-28.
//

#include "PluEngine/PluUtils.h"

#include <atomic>
#include <regex>
#include <sstream>

#include "glm/glm.hpp"
#include "glm/gtc/quaternion.hpp"

#ifdef PLU_PLATFORM_WINDOWS
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
	#include <windows.h>
#endif

namespace
{
	// Last published frame delta (seconds) per thread. Written by the owning loop, read from
	// anywhere (e.g. editor panels). Relaxed atomics are enough — these are diagnostics, no
	// ordering dependency on other state.
	std::atomic<float> gMainThreadDeltaTime{0.0f};
	std::atomic<float> gRenderThreadDeltaTime{0.0f};

	// Same rationale as above: published by the render thread at the end of each
	// Renderer::RenderSnapshot, read from editor panels on the main thread.
	std::atomic<UInt32> gStatDrawCalls{0};
	std::atomic<UInt32> gStatInstancesDrawn{0};
	std::atomic<UInt32> gStatCulledCount{0};

}

void Plu::SetMainThreadDeltaTime(float deltaSeconds)
{
	gMainThreadDeltaTime.store(deltaSeconds, std::memory_order_relaxed);
}

void Plu::SetRenderThreadDeltaTime(float deltaSeconds)
{
	gRenderThreadDeltaTime.store(deltaSeconds, std::memory_order_relaxed);
}

float Plu::GetMainThreadFPS()
{
	const float delta = gMainThreadDeltaTime.load(std::memory_order_relaxed);
	return delta > 0.0f ? 1.0f / delta : 0.0f;
}

float Plu::GetRenderThreadFPS()
{
	const float delta = gRenderThreadDeltaTime.load(std::memory_order_relaxed);
	return delta > 0.0f ? 1.0f / delta : 0.0f;
}

void Plu::SetRenderFrameStats(UInt32 drawCalls, UInt32 instancesDrawn, UInt32 culledCount)
{
	gStatDrawCalls.store(drawCalls, std::memory_order_relaxed);
	gStatInstancesDrawn.store(instancesDrawn, std::memory_order_relaxed);
	gStatCulledCount.store(culledCount, std::memory_order_relaxed);
}

UInt32 Plu::GetStatDrawCalls()
{
	return gStatDrawCalls.load(std::memory_order_relaxed);
}

UInt32 Plu::GetStatInstancesDrawn()
{
	return gStatInstancesDrawn.load(std::memory_order_relaxed);
}

UInt32 Plu::GetStatCulledCount()
{
	return gStatCulledCount.load(std::memory_order_relaxed);
}

Plu::PathW Plu::GetEngineResourcesDir()
{
#ifdef PLU_PROJECT_ROOT
	return StringW::FromNarrow(PLU_PROJECT_ROOT);
#else
	return GetExePath().GetParentPath();
#endif
}

Plu::PathW Plu::GetExePath()
{
#if defined(PLU_PLATFORM_WINDOWS)

	wchar_t buffer[MAX_PATH];
	DWORD size = GetModuleFileNameW(nullptr, buffer, MAX_PATH);

	if (size == 0) {
		throw std::runtime_error("GetModuleFileNameW failed");
	}

	return buffer;

#elif defined(PLU_PLATFORM_LINUX)

	char buffer[PATH_MAX];
	ssize_t len = readlink("/proc/self/exe", buffer, sizeof(buffer) - 1);

	if (len == -1) {
		throw std::runtime_error("readlink(/proc/self/exe) failed");
	}

	buffer[len] = '\0';
	return PathW(StringW::FromNarrow(buffer));

#endif
}

Plu::Path Plu::GetSystemUserPath()
{
#ifdef _WIN32
	const char* home = getenv("USERPROFILE");

	// Fallback: HOMEDRIVE + HOMEPATH (np. C: + \Users\Janek)
	if (!home) {
		const char* drive = getenv("HOMEDRIVE");
		const char* path  = getenv("HOMEPATH");

		if (drive && path) {
			String driveP(drive);
			String pathP(path);
			return driveP + pathP;
		}
		return "";
	}
#else
	const char* home = getenv("HOME");

	if (!home) return "";
#endif

	String pathS(home);
	return pathS;
}

Vec3 Plu::GetForwardVector(Vec3 rot)
{
	Quaternion qt = Quaternion(radians(rot));
	Vec3 forward = qt * Vec3(0, 0, -1);
	return glm::normalize(forward);
}

Vec3 Plu::GetRightVector(Vec3 rot)
{
	Quaternion qt = Quaternion(radians(rot));
	Vec3 right = qt * Vec3(1, 0, 0);
	return glm::normalize(right);
}

Vec3 Plu::GetUpVector(Vec3 rot)
{
	Quaternion qt = Quaternion(radians(rot));
	Vec3 up = qt * Vec3(0, 1, 0);
	return glm::normalize(up);
}

double Plu::ClampD(double val, double min, double max)
{
	return glm::clamp<double>(val, min, max);
}

float Plu::ClampF(float val, float min, float max)
{
	return glm::clamp<float>(val, min, max);
}

int Plu::ClampI(int val, int min, int max)
{
	return glm::clamp<int>(val, min, max);
}

float Plu::ClampAngle(float angle, float min, float max)
{
	return glm::clamp(NormalizeAxisDegrees(angle), min, max);
}

float Plu::NormalizeAxisDegrees(float angle)
{
	angle = glm::mod(angle, 360.0f);
	if (angle > 180.0f)
		angle -= 360.0f;
	return angle;
}

double Plu::LerpD(double val, double target, double alpha)
{
	return glm::mix<double>(val, target, alpha);
}

float Plu::LerpF(float val, float target, float alpha)
{
	return glm::mix<float>(val, target, alpha);
}

int Plu::LerpI(int val, int target, float alpha)
{
	return static_cast<int>(std::lround(glm::mix<float>(static_cast<float>(val), static_cast<float>(target), alpha)));
}

Vec3 Plu::LerpVec3(Vec3 val, Vec3 target, float alpha)
{
	return glm::mix(val, target, alpha);
}

double Plu::LerpClampedD(double val, double target, double alpha)
{
	return LerpD(val, target, ClampD(alpha, 0.0, 1.0));
}

float Plu::LerpClampedF(float val, float target, float alpha)
{
	return LerpF(val, target, ClampF(alpha, 0.0f, 1.0f));
}

int Plu::LerpClampedI(int val, int target, float alpha)
{
	return LerpI(val, target, ClampF(alpha, 0.0f, 1.0f));
}

Vec3 Plu::LerpClampedVec3(Vec3 val, Vec3 target, float alpha)
{
	return LerpVec3(val, target, ClampF(alpha, 0.0f, 1.0f));
}

namespace
{
	// Squared distance below which an InterpTo* call is considered "there" and snaps to the
	// target. Without it the eased chase only ever halves the remaining distance and the value
	// keeps crawling forever, denormal by denormal.
	constexpr float kInterpSnapDistSq = 1e-8f;
	constexpr double kInterpSnapDistSqD = 1e-16;
}

double Plu::InterpToD(double val, double target, double deltaTime, double interpSpeed)
{
	if (interpSpeed <= 0.0)
		return target;

	const double dist = target - val;
	if (dist * dist < kInterpSnapDistSqD)
		return target;

	return val + dist * ClampD(deltaTime * interpSpeed, 0.0, 1.0);
}

float Plu::InterpToF(float val, float target, float deltaTime, float interpSpeed)
{
	if (interpSpeed <= 0.0f)
		return target;

	const float dist = target - val;
	if (dist * dist < kInterpSnapDistSq)
		return target;

	return val + dist * ClampF(deltaTime * interpSpeed, 0.0f, 1.0f);
}

Vec3 Plu::InterpToVec3(Vec3 val, Vec3 target, float deltaTime, float interpSpeed)
{
	if (interpSpeed <= 0.0f)
		return target;

	const Vec3 dist = target - val;
	if (glm::dot(dist, dist) < kInterpSnapDistSq)
		return target;

	return val + dist * ClampF(deltaTime * interpSpeed, 0.0f, 1.0f);
}

Vec3 Plu::InterpToRotator(Vec3 val, Vec3 target, float deltaTime, float interpSpeed)
{
	if (interpSpeed <= 0.0f)
		return Vec3(NormalizeAxisDegrees(target.x), NormalizeAxisDegrees(target.y), NormalizeAxisDegrees(target.z));

	// Per axis, because the shortest way around is a per-axis question — wrapping the whole
	// vector at once would drag the other two axes the long way.
	const Vec3 dist = Vec3(
		NormalizeAxisDegrees(target.x - val.x),
		NormalizeAxisDegrees(target.y - val.y),
		NormalizeAxisDegrees(target.z - val.z));

	if (glm::dot(dist, dist) < kInterpSnapDistSq)
		return Vec3(NormalizeAxisDegrees(target.x), NormalizeAxisDegrees(target.y), NormalizeAxisDegrees(target.z));

	const Vec3 result = val + dist * ClampF(deltaTime * interpSpeed, 0.0f, 1.0f);
	return Vec3(NormalizeAxisDegrees(result.x), NormalizeAxisDegrees(result.y), NormalizeAxisDegrees(result.z));
}

Quaternion Plu::InterpToQuat(const Quaternion& val, const Quaternion& target, float deltaTime, float interpSpeed)
{
	if (interpSpeed <= 0.0f)
		return glm::normalize(target);

	return glm::normalize(glm::slerp(val, target, ClampF(deltaTime * interpSpeed, 0.0f, 1.0f))); // shortest arc, handled by glm
}

double Plu::InterpConstantToD(double val, double target, double deltaTime, double interpSpeed)
{
	const double dist = target - val;
	if (dist * dist < kInterpSnapDistSqD)
		return target;

	const double step = interpSpeed * deltaTime;
	if (step <= 0.0)
		return val;

	return val + ClampD(dist, -step, step);
}

float Plu::InterpConstantToF(float val, float target, float deltaTime, float interpSpeed)
{
	const float dist = target - val;
	if (dist * dist < kInterpSnapDistSq)
		return target;

	const float step = interpSpeed * deltaTime;
	if (step <= 0.0f)
		return val;

	return val + ClampF(dist, -step, step);
}

Vec3 Plu::InterpConstantToVec3(Vec3 val, Vec3 target, float deltaTime, float interpSpeed)
{
	const Vec3 dist = target - val;
	const float distLength = glm::length(dist);
	const float step = interpSpeed * deltaTime;

	// Already there, or the step would carry us past the target — land on it exactly.
	if (distLength <= step)
		return target;

	if (step <= 0.0f)
		return val;

	return val + (dist / distLength) * step;
}

Vec3 Plu::InterpConstantToRotator(Vec3 val, Vec3 target, float deltaTime, float interpSpeed)
{
	const float step = interpSpeed * deltaTime;
	if (step <= 0.0f)
		return Vec3(NormalizeAxisDegrees(val.x), NormalizeAxisDegrees(val.y), NormalizeAxisDegrees(val.z));

	// Each axis gets the full degrees/second budget on its own (that is the Unreal behaviour),
	// so a rotator turning on two axes is not slowed down by splitting the rate between them.
	return Vec3(
		NormalizeAxisDegrees(val.x + ClampF(NormalizeAxisDegrees(target.x - val.x), -step, step)),
		NormalizeAxisDegrees(val.y + ClampF(NormalizeAxisDegrees(target.y - val.y), -step, step)),
		NormalizeAxisDegrees(val.z + ClampF(NormalizeAxisDegrees(target.z - val.z), -step, step)));
}

Vec3 Plu::GetLookAtRotatorDegrees(const Vec3 &eye, const Vec3 &target)
{

	Vec3 direction = glm::normalize(target - eye);

	float yawRad = std::atan2(direction.x, direction.z);

	float horizontalLength = std::sqrt(direction.x * direction.x + direction.z * direction.z);
	float pitchRad = std::atan2(direction.y, horizontalLength);

	float yawDegrees = glm::degrees(yawRad);
	float pitchDegrees = glm::degrees(pitchRad);

	while (yawDegrees > 180.0f) {
		yawDegrees -= 360.0f;
	}
	while (yawDegrees <= -180.0f) {
		yawDegrees += 360.0f;
	}

	return {
		pitchDegrees, // X (Pitch - góra/dół)
		yawDegrees,   // Y (Yaw - lewo/prawo)
		0.0f          // Z (Roll)
	};
}

Vec3 Plu::GetRotatedPointWithRadius(const Vec3 &center, float radius, float angleDegrees,
	const Vec3 &axis)
{
	// Krok 1: Definicja Punktu Początkowego (Na osi X lub innej, 
	// aby łatwo było określić kierunek).
	// Załóżmy, że punkt P zaczyna w (radius, 0, 0) względem centrum (0, 0, 0).
	glm::vec4 initialPoint = glm::vec4(radius, 0.0f, 0.0f, 1.0f);
    
	// Krok 2: Utwórz macierz transformacji
	glm::mat4 transform = glm::mat4(1.0f);

	// --- A. Wykonaj Obrot (Rotacja R) ---
	// Obracamy nasz "wektor promienia" (radius, 0, 0).
	float angleRadians = glm::radians(angleDegrees);
	transform = glm::rotate(transform, angleRadians, axis);

	// Krok 3: Zastosuj obrót
	glm::vec4 rotatedDirection = transform * initialPoint;

	// Krok 4: Przesunięcie do centrum
	// Dodajemy współrzędne centrum, aby przesunąć obrócony punkt z powrotem.
	// Zapewnia to, że punkt jest oddalony o 'radius' od 'center'.
	Vec3 finalPoint = center + Vec3(rotatedDirection);

	return finalPoint;
}

Vec3 Plu::GetSphericalOrbitPoint(const Vec3 &center, float radius, float yawDegrees, float pitchDegrees)
{
	float yawRad = glm::radians(yawDegrees);
	float pitchRad = glm::radians(pitchDegrees);

	Quaternion pitchRotation = glm::angleAxis(pitchRad, Vec3(1.0f, 0.0f, 0.0f)); // oś X
	Quaternion yawRotation   = glm::angleAxis(yawRad,   Vec3(0.0f, 1.0f, 0.0f)); // oś Y

	Quaternion combinedRotation = yawRotation * pitchRotation;

	Vec3 initialOffset = Vec3(0.0f, 0.0f, radius); // forward = Z
	Vec3 rotatedOffset = combinedRotation * initialOffset;

	return center + rotatedOffset;
}

void Plu::NormalizeVec3Rotation(Vec3 *vec)
{
	auto normalizeAngle = [](float angle) -> float
	{
		angle = glm::mod(angle, 360.0f);
		if (angle < 0.0f)
			angle += 360.0f;
		return angle;
	};

	*vec = Vec3(
		normalizeAngle(vec->x),
		normalizeAngle(vec->y),
		normalizeAngle(vec->z)
	);
}

Vec3 Plu::GetLocationFromMatrix(const Matrix4 &matrix)
{
	return Vec3(matrix[3]);
}

Vec3 Plu::GetScaleFromMatrix(const Matrix4 &matrix)
{
	return Vec3(
		glm::length(Vec3(matrix[0])),
		glm::length(Vec3(matrix[1])),
		glm::length(Vec3(matrix[2]))
	);
}

Vec3 Plu::GetRotationFromMatrix(const Matrix4 &matrix)
{
	Vec3 scale = GetScaleFromMatrix(matrix);
	glm::mat3 rotMat = glm::mat3(
		Vec3(matrix[0]) / scale.x,
		Vec3(matrix[1]) / scale.y,
		Vec3(matrix[2]) / scale.z
	);
	return glm::degrees(glm::eulerAngles(glm::quat_cast(rotMat)));
}

Quaternion Plu::GetQuaternionFromEuler(Vec3 angles)
{
	return glm::quat(glm::radians(angles));
}

Plu::String Plu::MakeStringForDisplay(String text)
{
	static HashMap<String, String> stringCache;
	if (stringCache.Contains(text)) return stringCache[text];

	char lastChar = '\0';

	String display = "";

	for (auto c : text) {
		if (lastChar == '\0') {
			lastChar = c;
			display += c;
			continue;
		}
		if (std::isupper(c) && std::islower(lastChar)) {
			display += " ";
		}
		display += c;
		lastChar = c;
	}
	stringCache[text] = display;
	return display;
}

Plu::String Plu::PrepareCodeForDistribution(String code)
{
	std::string src = code.CStr();

	// Remove block comments /* */
	src = std::regex_replace(src, std::regex(R"(/\*[\s\S]*?\*/)"), "");

	std::istringstream stream(src);
	std::string result;
	std::string line;

	while (std::getline(stream, line)) {
		// Remove // comments
		auto commentPos = line.find("//");
		if (commentPos != std::string::npos)
			line = line.substr(0, commentPos);

		// Trim surrounding whitespace
		auto lineStart = line.find_first_not_of(" \t\r");
		if (lineStart == std::string::npos) continue;
		line = line.substr(lineStart);
		auto lineEnd = line.find_last_not_of(" \t\r");
		if (lineEnd != std::string::npos) line = line.substr(0, lineEnd + 1);

		if (line.empty()) continue;

		if (line[0] == '#') {
			// Preprocessor directive — GLSL requires these to be on their own line.
			// Encode surrounding newlines as \n literal so Shaders.txt stays single-line.
			result += "\\n" + line + "\\n";
		} else {
			line = std::regex_replace(line, std::regex(R"(\s+)"), " ");
			result += line + " ";
		}
	}

	// Collapse multiple spaces that may appear at glue points
	result = std::regex_replace(result, std::regex(R"( {2,})"), " ");

	// Trim leading/trailing whitespace
	auto trimStart = result.find_first_not_of(" ");
	if (trimStart == std::string::npos) return "";
	result = result.substr(trimStart);
	while (!result.empty() && result.back() == ' ') result.pop_back();

	return result.c_str();
}
