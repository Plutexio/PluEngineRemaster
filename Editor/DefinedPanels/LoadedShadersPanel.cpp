//
// Created by Plutex on 2026-06-26.
//

#include "LoadedShadersPanel.h"
#include "PluEngine/FrameDemand.h"

#include "imgui.h"
#include "PluEngine/Application.h"
#include "PluEngine/Render/ShadersManager.h"
#include "PluEngine/Render/ShaderProgram.h"
#include "PluEngine/AssetTypes/ShaderCode.h"
#include "UI/IconsFontAwesome7.h"

Plu::String Plu::LoadedShadersPanel::GetPanelName()
{
	return ICON_FA_WAND_MAGIC_SPARKLES " Loaded Shaders";
}

void Plu::LoadedShadersPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel()) {
		// Live numbers, no input behind them: keep them ticking under power saving.
		RequestRedrawAfter(kStatsRefreshSeconds);
		if (!mApplicationInfo->AppShaderManager) {
			ImGui::TextDisabled("No shader manager available.");
			EndPanel();
			return;
		}

		DynamicArray<TUsePointer<ShaderProgram>>* programs = mApplicationInfo->AppShaderManager->GetRenderableShaderPrograms();
		ImGui::Text("Renderable shader programs: %d", programs ? static_cast<int>(programs->Size()) : 0);
		ImGui::Separator();

		if (programs) {
			for (UInt32 i = 0; i < programs->Size(); i++) {
				TUsePointer<ShaderProgram> program = (*programs)[i];
				if (!program) {
					continue;
				}

				ImGui::PushID(static_cast<int>(i));

				TUsePointer<IShaderCode> vertex = program->GetVertexShader();
				TUsePointer<IShaderCode> fragment = program->GetFragmentShader();

				String header = String("Program ") + program->Uuid.toString();
				header += program->IsLoaded() ? "  [loaded]" : "  [not loaded]";

				if (ImGui::TreeNode(header.CStr())) {
					ImGui::Text("UUID: %s", program->Uuid.toString().CStr());
					ImGui::Text("Loaded: %s", program->IsLoaded() ? "yes" : "no");
					ImGui::Text("Engine slots used: %d", program->GetSlotsUsed());
					ImGui::Text("Vertex: %s", vertex ? vertex->Name.CStr() : "<none>");
					ImGui::Text("Fragment: %s", fragment ? fragment->Name.CStr() : "<none>");

					TUsePointer<IShaderCode> codes[2] = { vertex, fragment };
					const char* codeLabels[2] = { "Vertex", "Fragment" };
					for (int c = 0; c < 2; c++) {
						TUsePointer<IShaderCode> code = codes[c];
						if (!code) {
							continue;
						}
						String stageHeader = String(codeLabels[c]) + " - " + code->Name;
						if (ImGui::TreeNode(stageHeader.CStr())) {
							DynamicArray<TOwningPointer<IShaderUniform>>* uniforms = code->GetCodeUniforms();
							if (uniforms && !uniforms->IsEmpty()) {
								ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable;
								if (ImGui::BeginTable("##uniforms", 3, tableFlags)) {
									ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
									ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed);
									ImGui::TableSetupColumn("Array", ImGuiTableColumnFlags_WidthFixed);
									ImGui::TableHeadersRow();
									for (UInt32 u = 0; u < uniforms->Size(); u++) {
										TOwningPointer<IShaderUniform>& uniform = (*uniforms)[u];
										if (!uniform) {
											continue;
										}
										ImGui::TableNextRow();
										ImGui::TableNextColumn();
										ImGui::Text("%s", uniform->Name.CStr());
										ImGui::TableNextColumn();
										ImGui::Text("%s", uniform->Type.CStr());
										ImGui::TableNextColumn();
										ImGui::Text("%d", uniform->ArraySize);
									}
									ImGui::EndTable();
								}
							} else {
								ImGui::TextDisabled("No uniforms.");
							}

							if (ImGui::TreeNode("Source")) {
								String source = code->GetCode();
								ImGui::TextWrapped("%s", source.CStr());
								ImGui::TreePop();
							}
							ImGui::TreePop();
						}
					}
					ImGui::TreePop();
				}
				ImGui::PopID();
			}
		}
	}
	EndPanel();
}

void Plu::LoadedShadersPanel::OnHide()
{
}

void Plu::LoadedShadersPanel::OnShow()
{
	SetCanClose(true);
}
