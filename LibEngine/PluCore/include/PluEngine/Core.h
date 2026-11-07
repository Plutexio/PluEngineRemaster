//
// Created by Plutex on 12/30/25.
//

#ifndef PLUENGINE_CORE_H
#define PLUENGINE_CORE_H

// Integer aliases, platform detection, PLU_EXPORT/PLU_IMPORT and the asserts live in PluBase.
#include "PluBase/Base.h"
#include "PluBase/Assert.h"

// ---------------------------------------------------------------------------
// Engine module visibility.
//
// Each engine module owns an export macro of its own, because one macro cannot
// serve several shared libraries: while compiling PluRender its own symbols must
// be dllexport while PluCore's must be dllimport, and a single macro cannot
// expand two ways inside one translation unit. Each module's build defines only
// its own PLU_BUILD_PLU* flag (plu_add_engine_module), so everything it uses
// from the modules below resolves to an import.
// ---------------------------------------------------------------------------

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