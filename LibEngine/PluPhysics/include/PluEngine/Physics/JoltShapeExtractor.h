//
// Created by Plutex on 2026-03-09.
//

#ifndef PLUENGINE_JOLTSHAPEEXTRACTOR_H
#define PLUENGINE_JOLTSHAPEEXTRACTOR_H

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Geometry/AABox.h>
#include <glm/glm.hpp>
#include "PluSTL/PluSTL_FWD.h"

namespace Plu
{
	class JoltShapeExtractor
	{
	protected:
		static DynamicArray<glm::vec3> ExtractTriangles(const JPH::Shape* shape);
		static glm::mat4 JoltToGlm(const JPH::RMat44& m);
	};
}

#endif //PLUENGINE_JOLTSHAPEEXTRACTOR_H