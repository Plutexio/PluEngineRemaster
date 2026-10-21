//
// Created by Plutex on 12/30/25.
//
// Fixed-width integer aliases, platform detection, the DLL visibility macros and
// the debug break. No dependencies beyond <cstdint>, so any header can afford it.
//

#ifndef PLUBASE_BASE_H
#define PLUBASE_BASE_H

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
// Every shared library built on PluBase defines an API macro of its own on top of
// these two, the way PLUBASE_API does below: while compiling a library its own
// symbols must be dllexport while the ones it uses from another library must be
// dllimport, and a single macro cannot expand two ways inside one translation unit.
// ---------------------------------------------------------------------------
#ifdef PLU_PLATFORM_WINDOWS
	#define PLU_EXPORT __declspec(dllexport)
	#define PLU_IMPORT __declspec(dllimport)
#else
	#define PLU_EXPORT __attribute__((visibility("default")))
	#define PLU_IMPORT __attribute__((visibility("default")))
#endif

#ifdef PLU_BUILD_PLUBASE
	#define PLUBASE_API PLU_EXPORT
#else
	#define PLUBASE_API PLU_IMPORT
#endif

// PLU_DEBUG (set by the PluBase target for Debug configurations) turns asserts on.
// Define PLU_ENABLE_ASSERTS yourself to keep them in other configurations too.
#ifdef PLU_DEBUG
	#if defined(PLU_PLATFORM_WINDOWS)
		#define PLU_DEBUGBREAK() __debugbreak()
	#else
		#include <csignal>
		#define PLU_DEBUGBREAK() raise(SIGTRAP)
	#endif
	#ifndef PLU_ENABLE_ASSERTS
		#define PLU_ENABLE_ASSERTS
	#endif
#else
	#define PLU_DEBUGBREAK()
#endif

#endif //PLUBASE_BASE_H
