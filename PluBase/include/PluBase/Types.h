//
// Created by Plutex on 11.01.2026.
//
// Math type aliases over glm, in the global namespace. The integer aliases come
// from Base.h; GlmHash.h lets every one of these key a PluSTL HashMap.
//

#ifndef PLUBASE_TYPES_H
#define PLUBASE_TYPES_H

#include "PluBase/Base.h"

#include "glm/fwd.hpp"
#include "glm/vec3.hpp"
#include "glm/vec4.hpp"
#include "glm/vec2.hpp"
#include "glm/mat4x4.hpp"
#include "glm/detail/type_quat.hpp"
#include "PluBase/GlmHash.h"

typedef glm::vec2 Vec2;
typedef glm::vec3 Vec3;
typedef glm::vec4 Vec4;

typedef glm::ivec2 IVec2;
typedef glm::ivec3 IVec3;
typedef glm::ivec4 IVec4;

typedef glm::mat4 Matrix4;

typedef glm::quat Quaternion;

#endif //PLUBASE_TYPES_H
