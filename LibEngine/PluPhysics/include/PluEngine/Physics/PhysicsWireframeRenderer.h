//
// Created by Plutex on 2026-03-09.
//

#ifndef PLUENGINE_PHYSICSWIREFRAMERENDERER_H
#define PLUENGINE_PHYSICSWIREFRAMERENDERER_H

#include <glm/glm.hpp>
#include "PluEngine/Physics/JoltShapeExtractor.h"
#include "PluSTL/PluSTL_FWD.h"

namespace JPH
{
	class Body;
}

namespace Plu
{
	// Czysty ekstraktor geometrii (CPU/Jolt) — bez GL. Używany na MAIN przy budowie
	// snapshotu: BeginFrame/AddBody/AddShape budują linie, PackInto pakuje je do
	// płaskiego bufora (pos+color interleaved), który rysuje wątek renderu.
	class JoltWireframeRenderer : public JoltShapeExtractor
	{
	public:
		JoltWireframeRenderer() = default;
		~JoltWireframeRenderer() = default;

		void BeginFrame();
		void AddBody(const JPH::Body& body, const glm::vec3& color = { 0.0f, 1.0f, 0.0f });
		void AddShape(const JPH::ShapeRefC& shape, const glm::mat4& transform, const glm::vec3& color = { 0.0f, 1.0f, 0.0f });
		// Dopisuje linie jako pary wierzchołków pos(3)+color(3) do out (GL_LINES).
		void PackInto(DynamicArray<float>* out) const;

	private:
		struct Line
		{
			glm::vec3 a, b;
			glm::vec3 color;
		};

		DynamicArray<Line> m_lines;
	};
}

#endif //PLUENGINE_PHYSICSWIREFRAMERENDERER_H