//
// Created by Plutex on 2026-06-20.
//

#include "ProfilerPanel.h"
#include "PluEngine/FrameDemand.h"

#include "imgui.h"
#include "nfd.h"
#include "PluEngine/Core/DiskManager.h"
#include "PluEngine/PluUtils.h"
#include "PluEngine/Profiler.h"
#include "UI/IconsFontAwesome7.h"

Plu::String Plu::ProfilerPanel::GetPanelName()
{
	return ICON_FA_STOPWATCH " Profiler";
}

void Plu::ProfilerPanel::OnShow()
{
	SetCanClose(true);
}

void Plu::ProfilerPanel::ExportCsv()
{
	nfdu8char_t* outPath = nullptr;
	const nfdu8filteritem_t filters[1] = { { "CSV", "csv" } };
	if (NFD_SaveDialogU8(&outPath, filters, 1, nullptr, "profiler.csv") != NFD_OKAY) {
		return; // cancelled or failed — nothing to report
	}
	String path = outPath;
	NFD_FreePathU8(outPath);

	// The dialog does not force the extension on every platform, so make sure we write a .csv.
	if (!path.EndsWith(".csv")) {
		path += ".csv";
	}

	// The export honours the thread filter, so what lands in the file is what the table shows.
	const String csv = Profiler::GetInstance()->BuildCsv(mThreadFilter);
	if (DiskManager::SaveText(path.ToWide(), csv)) {
		mExportStatus = "Exported to " + path;
		PLU_CORE_INFO("Profiler exported to {}", path.CStr());
	} else {
		mExportStatus = "Export failed: " + path;
		PLU_CORE_ERROR("Profiler export to {} failed", path.CStr());
	}
}

void Plu::ProfilerPanel::OnHide()
{
}

void Plu::ProfilerPanel::OnUpdate(float deltaTime)
{
	if (BeginPanel()) {
		// Live numbers, no input behind them: keep them ticking under power saving.
		RequestRedrawAfter(kStatsRefreshSeconds);
		const float mainFPS = GetMainThreadFPS();
		const float renderFPS = GetRenderThreadFPS();
		ImGui::Text("Main: %.1f FPS (%.2f ms)", mainFPS, mainFPS > 0.0f ? 1000.0f / mainFPS : 0.0f);
		ImGui::SameLine();
		ImGui::Text("| Render: %.1f FPS (%.2f ms)", renderFPS, renderFPS > 0.0f ? 1000.0f / renderFPS : 0.0f);
		ImGui::SameLine();
		if (ImGui::Button("Clear")) {
			Profiler::GetInstance()->Clear();
			mThreadFilter.Clear();
		}
		ImGui::SameLine();
		if (ImGui::Button(ICON_FA_FILE_CSV " Export CSV")) {
			ExportCsv();
		}
		if (ImGui::IsItemHovered()) {
			ImGui::SetTooltip("Exports the table as CSV, including each timer's full sample history.\n"
				"The active thread filter applies to the export as well.");
		}

		// Filtr po wątku, z którego pochodzi wpis. Lista wątków jest brana z aktualnych
		// pomiarów, więc pojawia się dopiero, gdy dany wątek cokolwiek zarejestrował.
		DynamicArray<String> threadNames = Profiler::GetInstance()->SnapshotThreadNames();
		ImGui::SameLine();
		ImGui::SetNextItemWidth(160.0f);
		const char* filterPreview = mThreadFilter.IsEmpty() ? "All threads" : mThreadFilter.CStr();
		if (ImGui::BeginCombo("##threadfilter", filterPreview)) {
			if (ImGui::Selectable("All threads", mThreadFilter.IsEmpty())) {
				mThreadFilter.Clear();
			}
			for (const String& threadName : threadNames) {
				if (ImGui::Selectable(threadName.CStr(), mThreadFilter == threadName)) {
					mThreadFilter = threadName;
				}
			}
			ImGui::EndCombo();
		}

		if (!mExportStatus.IsEmpty()) {
			ImGui::TextDisabled("%s", mExportStatus.CStr());
		}

		ImGui::Separator();

		HashMap<String, ProfilerEntry> snapshot = Profiler::GetInstance()->Snapshot();

		ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
			ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY;

		if (ImGui::BeginTable("##timings", 8, tableFlags)) {
			ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableSetupColumn("Thread", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Last (ms)", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Avg (ms)", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Min (ms)", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Max (ms)", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("Calls", ImGuiTableColumnFlags_WidthFixed);
			ImGui::TableSetupColumn("History", ImGuiTableColumnFlags_WidthFixed, 140.0f);
			ImGui::TableHeadersRow();

			for (auto& pair : snapshot) {
				const String& key = pair.first;
				ProfilerEntry& entry = pair.second;

				if (!mThreadFilter.IsEmpty() && entry.ThreadName != mThreadFilter) continue;

				ImGui::TableNextRow();

				ImGui::TableNextColumn();
				ImGui::Text("%s", entry.Name.CStr());
				ImGui::TableNextColumn();
				ImGui::Text("%s", entry.ThreadName.CStr());
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", entry.LastMs);
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", entry.AvgMs);
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", entry.MinMs);
				ImGui::TableNextColumn();
				ImGui::Text("%.3f", entry.MaxMs);
				ImGui::TableNextColumn();
				ImGui::Text("%llu", static_cast<unsigned long long>(entry.TotalCalls));
				ImGui::TableNextColumn();
				// WriteIndex jako offset ring-buffera; skala 0..Max dla czytelności.
				// Klucz (wątek|nazwa) jako ID — sama nazwa kolidowałaby między wątkami.
				String sparkId = "##spark_" + key;
				ImGui::PlotLines(sparkId.CStr(), entry.History, entry.SampleCount, entry.WriteIndex,
					nullptr, 0.0f, entry.MaxMs > 0.0f ? entry.MaxMs : 1.0f, ImVec2(130.0f, 30.0f));
			}
			ImGui::EndTable();
		}
	}
	EndPanel();
}
