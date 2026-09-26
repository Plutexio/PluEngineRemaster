//
// Created by Plutex on 9/26/26.
//

#include "PluEngine/Effects/Particles/ParticleParam.h"

#ifdef PLU_ENGINE_EDITOR_BUILD
#include "imgui.h"

namespace Plu
{
	bool ParticleParamColorEditorControl(ParticleParamColor& param, const String& label)
	{
		bool changed = false;
		ImGui::PushID(&param);
		// "##..." is the hidden-label convention: the caller already names the field.
		const bool hiddenLabel = label.Length() >= 2 && label.CStr()[0] == '#' && label.CStr()[1] == '#';
		if (!hiddenLabel) ImGui::TextUnformatted(label.CStr());

		constexpr ImGuiColorEditFlags kPickerFlags = ImGuiColorEditFlags_AlphaBar | ImGuiColorEditFlags_AlphaPreviewHalf;

		int mode = static_cast<int>(param.Mode);
		const char* modeNames[] = { "Constant", "Random Range", "Gradient over attribute" };
		ImGui::SetNextItemWidth(-FLT_MIN);
		if (ImGui::Combo("##mode", &mode, modeNames, 3)) {
			param.Mode = static_cast<EParticleParamMode>(mode);
			changed = true;
		}

		switch (param.Mode) {
			case EParticleParamMode::Constant:
				ImGui::SetNextItemWidth(-FLT_MIN);
				changed |= ImGui::ColorEdit4("##value", &param.Value.x, kPickerFlags);
				break;
			case EParticleParamMode::RandomRange:
				// Each channel is drawn independently between Min and Max.
				changed |= ImGui::ColorEdit4("Min", &param.Min.x, kPickerFlags);
				changed |= ImGui::ColorEdit4("Max", &param.Max.x, kPickerFlags);
				break;
			case EParticleParamMode::CurveOverSource: {
				changed |= GradientEditorControl(param.Gradient, "##gradient");
				// Only scalar attributes can drive a gradient; Seed gives each particle a random pick from it.
				const EParticleAttribute sources[] = { EParticleAttribute::NormalizedAge, EParticleAttribute::Age,
				                                       EParticleAttribute::Speed, EParticleAttribute::Seed };
				const char* sourceNames[] = { "Normalized Age", "Age (s)", "Speed (m/s)", "Seed (random per particle)" };
				int current = 0;
				for (int i = 0; i < 4; ++i) if (sources[i] == param.Source) current = i;
				ImGui::SetNextItemWidth(-FLT_MIN);
				if (ImGui::Combo("##source", &current, sourceNames, 4)) {
					param.Source = sources[current];
					changed = true;
				}
				// Normalized Age and Seed are already 0..1; Age and Speed map [0, SourceRange] onto the gradient.
				if (param.Source == EParticleAttribute::Age || param.Source == EParticleAttribute::Speed) {
					ImGui::SetNextItemWidth(-FLT_MIN);
					changed |= ImGui::DragFloat("##range", &param.SourceRange, 0.05f, 0.001f, 1000.0f, "Range 0 .. %.2f");
				}
				break;
			}
		}

		ImGui::PopID();
		return changed;
	}
}
#else
namespace Plu
{
	bool ParticleParamColorEditorControl(ParticleParamColor&, const String&) { return false; }
}
#endif
