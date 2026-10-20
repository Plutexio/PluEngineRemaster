//
// Created by Plutex on 2026-03-09.
//

#ifndef PLUENGINE_PHYSICSPOINTRENDERER_H
#define PLUENGINE_PHYSICSPOINTRENDERER_H

#include <glm/glm.hpp>
#include "PluEngine/Physics/JoltShapeExtractor.h"
#include "PluSTL/PluSTL_FWD.h"

namespace Plu
{
	// Czysty ekstraktor punktów (CPU/Jolt) — bez GL. Patrz JoltWireframeRenderer.
	class JoltPointRenderer : public JoltShapeExtractor
	{
	public:
		JoltPointRenderer() = default;
		~JoltPointRenderer() = default;

		void BeginFrame();
		void AddBody(const JPH::Body& body, const glm::vec3& color = { 1.0f, 0.3f, 0.0f });
		void AddShape(const JPH::ShapeRefC& shape, const glm::mat4& transform, const glm::vec3& color = { 1.0f, 0.3f, 0.0f });
		// Dopisuje punkty jako wierzchołki pos(3)+color(3) do out (GL_POINTS).
		void PackInto(DynamicArray<float>* out) const;

	private:
		struct Point
		{
			glm::vec3 pos;
			glm::vec3 color;
		};

		DynamicArray<Point> m_points;
	};
}

#endif //PLUENGINE_PHYSICSPOINTRENDERER_H