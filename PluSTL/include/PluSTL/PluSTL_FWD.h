#ifndef PLUSTL_FWD_H
#define PLUSTL_FWD_H

#include "PluSTL/Pointers/ControlBlock.h"
#include "PluSTL/Pointers/Casts.h"

//Pointers
#include "PluSTL/Pointers/TOwningPointer.h"
#include "PluSTL/Pointers/TUsePointer.h"

//Hashers
#include "PluSTL/Hashers/Default.h"
#include "PluSTL/Hashers/String.h"

//Random
#include "PluSTL/Random/Random.h"

//Arrays
#include "PluSTL/Array/Array.h"

//Queues
#include "PluSTL/Queue/Queue.h"

//Strings
#include "PluSTL/String/String.h"
#include "PluSTL/Path/Path.h"

//HashMaps
#include "PluSTL/HashMap/HashMap.h"
//HashSet
#include "PluSTL/HashSet/HashSet.h"

// Concurrent/ is deliberately NOT included here. This header is the precompiled header
// for both Engine (LibEngine/CMakeLists.txt) and PluEditor (Editor/CMakeLists.txt), so
// anything added here lands in every translation unit in the project — and the concurrent
// containers pull in <atomic>, <mutex>, <shared_mutex> and <thread>. Only a handful of
// files need them, and those include the specific header they use:
//   #include "PluSTL/Concurrent/ConcurrentHashMap.h"   (also: ...HashSet / ...Queue / ...Array /
//                                                ...String / LockPrimitives.h, or
//                                                Concurrent/Concurrent.h for all of them)
// Please do not "fix" this by adding them above.
//
// (ConcurrentHashMap.h does reach every engine TU anyway, because Profiler.h holds one and
// Timer.h — itself a PCH — includes Profiler.h. That is one header by an explicit decision;
// it is not a reason to make the other four unconditional, and PluSTL consumers outside the
// engine, e.g. Tests/, pay for none of them.)

#endif
