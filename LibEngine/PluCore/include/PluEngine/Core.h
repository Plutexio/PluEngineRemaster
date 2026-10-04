//
// Created by Plutex on 12/30/25.
//

#ifndef PLUENGINE_CORE_H
#define PLUENGINE_CORE_H

#include <cstdint>

using UInt64 = std::uint64_t;
using UInt32 = std::uint32_t;
using UInt16 = std::uint16_t;
using UInt8 = std::uint8_t;

using Int8 = std::int8_t;
using Int16 = std::int16_t;
using Int32 = std::int32_t;
using Int64 = std::int64_t;


#ifdef _WIN32
	#ifdef _WIN64
		#define PLU_PLATFORM_WINDOWS
	#else
		#error "32-bit OSes are not supported!"
	#endif
#elif defined(__linux__)
	#define PLU_PLATFORM_LINUX
#elif defined(__APPLE__) || defined(__MACH__)
	#include <TargetConditionals.h>
	#if TARGET_OS_IPHONE
		#define PLU_PLATFORM_IOS
	#elif TARGET_OS_MAC
		#define PLU_PLATFORM_MACOS
	#else
		#error "Unknown Apple Platform!"
	#endif
#else
	#error "Unknown Platform!"
#endif

// ---------------------------------------------------------------------------
// DLL visibility.
//
// Each engine module owns an export macro of its own, because one macro cannot
// serve several shared libraries: while compiling PluRender its own symbols must
// be dllexport while PluCore's must be dllimport, and a single macro cannot
// expand two ways inside one translation unit.
//
// The modules still link into a single binary, so the build defines every
// PLU_BUILD_PLU* flag at once and every macro resolves to export -- exactly what
// PLU_API did on its own. Once the modules become separate libraries, each one
// defines only its own flag and the imports start resolving as imports.
// ---------------------------------------------------------------------------
#ifdef PLU_PLATFORM_WINDOWS
	#define PLU_EXPORT __declspec(dllexport)
	#define PLU_IMPORT __declspec(dllimport)
#else
	#define PLU_EXPORT __attribute__((visibility("default")))
	#define PLU_IMPORT __attribute__((visibility("default")))
#endif

// Retained for game code and any consumer outside the engine modules.
#ifdef PLU_BUILD_DLL
	#define PLU_API PLU_EXPORT
#else
	#define PLU_API PLU_IMPORT
#endif


#ifdef PLU_BUILD_PLUCORE
	#define PLUCORE_API PLU_EXPORT
#else
	#define PLUCORE_API PLU_IMPORT
#endif

#ifdef PLU_BUILD_PLUPLATFORM
	#define PLUPLATFORM_API PLU_EXPORT
#else
	#define PLUPLATFORM_API PLU_IMPORT
#endif

#ifdef PLU_BUILD_PLUASSETCORE
	#define PLUASSETCORE_API PLU_EXPORT
#else
	#define PLUASSETCORE_API PLU_IMPORT
#endif

#ifdef PLU_BUILD_PLUASSETTYPES
	#define PLUASSETTYPES_API PLU_EXPORT
#else
	#define PLUASSETTYPES_API PLU_IMPORT
#endif

#ifdef PLU_BUILD_PLURENDER
	#define PLURENDER_API PLU_EXPORT
#else
	#define PLURENDER_API PLU_IMPORT
#endif

#ifdef PLU_BUILD_PLUPHYSICS
	#define PLUPHYSICS_API PLU_EXPORT
#else
	#define PLUPHYSICS_API PLU_IMPORT
#endif

#ifdef PLU_BUILD_PLUSCRIPTING
	#define PLUSCRIPTING_API PLU_EXPORT
#else
	#define PLUSCRIPTING_API PLU_IMPORT
#endif

#ifdef PLU_BUILD_PLUASSETPIPELINE
	#define PLUASSETPIPELINE_API PLU_EXPORT
#else
	#define PLUASSETPIPELINE_API PLU_IMPORT
#endif

#ifdef PLU_BUILD_PLUGAMEPLAY
	#define PLUGAMEPLAY_API PLU_EXPORT
#else
	#define PLUGAMEPLAY_API PLU_IMPORT
#endif

#ifdef PLU_BUILD_PLUAPP
	#define PLUAPP_API PLU_EXPORT
#else
	#define PLUAPP_API PLU_IMPORT
#endif

#ifdef PLU_BUILD_PLUEFFECTS
#define PLUEFFECTS_API PLU_EXPORT
#else
#define PLUEFFECTS_API PLU_IMPORT
#endif

#ifdef PLU_DEBUG
	#if defined(PLU_PLATFORM_WINDOWS)
		#define PLU_DEBUGBREAK() __debugbreak()
	#elif defined(PLU_PLATFORM_LINUX)
		#include <csignal>
		#define PLU_DEBUGBREAK() raise(SIGTRAP)
	#endif
	#define PLU_ENABLE_ASSERTS
#else
	#define PLU_DEBUGBREAK()
#endif

#ifdef PLU_ENABLE_ASSERTS
	#define PLU_ASSERT(x, ...) { if(!(x)) { PLU_ERROR("Assertion Failed: {0}", __VA_ARGS__); PLU_DEBUGBREAK(); } }
	#define PLU_CORE_ASSERT(x, ...) { if(!(x)) { PLU_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); PLU_DEBUGBREAK(); } }
#else
	#define PLU_ASSERT(x, ...)
	#define PLU_CORE_ASSERT(x, ...)
#endif

#define PLU_CLASS(...)
#define PLU_INTERFACE(...)
#define PLU_STRUCT(...)
#define PLU_ENUM(...)

//PLU_PROPERTY macro for in-engine reflection
//Args:
//	UuidFor="" - Requires type as PluUUID, specifies for what class is the uuid for in-editor UI
#define PLU_PROPERTY(...)
#define PLU_FUNCTION(...)

#endif //PLUENGINE_CORE_H