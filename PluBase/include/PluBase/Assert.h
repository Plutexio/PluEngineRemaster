//
// Created by Plutex on 12/30/25.
//
// Asserts report through the loggers in Log.h and then break into the debugger.
// Compiled out unless PLU_ENABLE_ASSERTS is defined (see Base.h).
//

#ifndef PLUBASE_ASSERT_H
#define PLUBASE_ASSERT_H

#include "PluBase/Base.h"
#include "PluBase/Log.h"

#ifdef PLU_ENABLE_ASSERTS
	#define PLU_ASSERT(x, ...) { if(!(x)) { PLU_ERROR("Assertion Failed: {0}", __VA_ARGS__); PLU_DEBUGBREAK(); } }
	#define PLU_CORE_ASSERT(x, ...) { if(!(x)) { PLU_CORE_ERROR("Assertion Failed: {0}", __VA_ARGS__); PLU_DEBUGBREAK(); } }
#else
	#define PLU_ASSERT(x, ...)
	#define PLU_CORE_ASSERT(x, ...)
#endif

#endif //PLUBASE_ASSERT_H
