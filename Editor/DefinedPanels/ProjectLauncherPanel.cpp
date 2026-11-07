//
// Created by Plutex on 2026-02-16.
//

#include "ProjectLauncherPanel.h"

#include "EditorAppContext.h"
#include "nfd.h"
#include "Managers/Project/EditorProjectManager.h"
#include "Managers/Python/EditorPythonManager.h"
#include "PluEngine/Application.h"
#include "PluEngine/PluPaths.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/Platform/Window.h"
#include "UI/IconsFontAwesome7.h"
#include "PluEngine/Core/Widgets/Buttons.h"

Plu::String Plu::ProjectLauncherPanel::GetPanelName()
{
	return ICON_FA_SIGNAL " Project Launcher";
}

void Plu::ProjectLauncherPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel()) {
		ImVec2 avail = ImGui::GetContentRegionAvail();
		ImVec2 buttonSize = ImVec2(50 * (ImGui::GetFontSize() / 13), 50 * (ImGui::GetFontSize() / 13));
		ImVec2 recentProjectsSize = ImVec2(300, 500);
		ImGui::SetCursorPos(ImVec2((avail.x / 2) - ((buttonSize.x / 2) * 2 + ImGui::GetStyle().ItemSpacing.x), (avail.y / 2) - (buttonSize.y / 2) ));
		ImVec2 cursorStart = ImGui::GetCursorPos();
		if (ImGui::Button("New", buttonSize)) {
			mEditorAppContext->NewProjectPopup = true;
		}
		ImGui::SameLine();
		if (ImGuiWidgets::PrimaryButton("Open", buttonSize)) {
			nfdu8char_t* outPath = nullptr;
			const nfdu8filteritem_t filters[1] = { { "Plu Project", "pluproject" } };
			if (NFD_OpenDialogU8(&outPath, filters, 1, nullptr) == NFD_OKAY) {
				mEditorAppContext->EditorProjectManager->OpenProject(StringW::FromNarrow(outPath));
				NFD_FreePathU8(outPath);
			}
		}
		ImGui::SameLine();
		ImGui::SetCursorPos(ImVec2(cursorStart.x + buttonSize.x * 2 + ImGui::GetStyle().ItemSpacing.x * 2, cursorStart.y));
		ImGui::SetCursorPosY(ImGui::GetCursorPosY() + buttonSize.y / 2 - avail.y / 4);
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_HorizontalScrollbar | ImGuiWindowFlags_MenuBar;
		ImGui::BeginChild("RecentProjectsChild", ImVec2(ImGui::GetContentRegionAvail().x, avail.y / 2), ImGuiChildFlags_Borders, window_flags);
		if (ImGui::BeginMenuBar()) {
			ImGui::Text("Recent Projects");
			ImGui::EndMenuBar();
		}
		static nlohmann::json json = DiskManager::LoadJson(EditorProjectManager::GetRecentProjectsJSONPath());
		for (const auto& project : json["projects"]) {
			Path projectPath = project.get<std::string>().c_str();
			if (ImGui::Selectable(projectPath.GetStem().CStr())) {
				mEditorAppContext->EditorProjectManager->OpenProject(StringW::FromNarrow(projectPath.CStr()));
			}
		}
		ImGui::EndChild();
	}
	EndPanel();
}

void Plu::ProjectLauncherPanel::OnHide()
{
}

void Plu::ProjectLauncherPanel::OnShow()
{
	nlohmann::json json = DiskManager::LoadJson(EditorProjectManager::GetRecentProjectsJSONPath());
	nlohmann::json newProjects = {};
	newProjects["projects"] = nlohmann::json::array();
	for (const auto& project : json["projects"]) {
		if (std::filesystem::exists(project.get<std::string>().c_str())) {
			newProjects["projects"].push_back(project.get<std::string>().c_str());
		}
	}
	DiskManager::SaveJson(EditorProjectManager::GetRecentProjectsJSONPath().ToString(),newProjects);
}
