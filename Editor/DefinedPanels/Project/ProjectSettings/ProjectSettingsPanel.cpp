//
// Created by Plutex on 5/30/26.
//

#include "ProjectSettingsPanel.h"

#include "EditorAppContext.h"
#include "Managers/Project/EditorProjectManager.h"
#include "PluEngine/Gameplay/PluGame.h"
#include "PluEngine/Gameplay/Physics/PhysicsChannels.h"
#include "PluEngine/Timer.h"
#include "PluEngine/Core/Reflection/TypeTraits.h"
#include "UI/IconsFontAwesome7.h"
#include "String/String.h"
#include "Array/Array.h"
#include <cstdio>
#include <cfloat>

Plu::String Plu::ProjectSettingsPanel::GetPanelName()
{
    return ICON_FA_GEARS " Project Settings";
}

void Plu::ProjectSettingsPanel::OnHide()
{
}

void Plu::ProjectSettingsPanel::OnShow()
{
    SetCanClose(true);
}

void Plu::ProjectSettingsPanel::OnUpdate(float deltaTime)
{
    if (BeginPanel()) {
        if (!mEditorAppContext->EditorProjectManager->IsAnyProjectOpen())
        {
            ImGui::Text("Open Project before browsing settings!");
            EndPanel();
            return;
        }
        TypeSerializer<TypeInfo*>::EditorControl(GameStartupSettings::GetStaticClass(), mEditorAppContext->EditorProjectManager->GetGameStartupSettings().GetRaw());
        DrawPhysicsChannels();
    }
    EndPanel();
}

namespace
{
    // Tint per response (Ignore / Overlap / Block) — reused for headers + radio checkmarks.
    const ImVec4 kResponseColors[3] = {
        ImVec4(0.78f, 0.42f, 0.42f, 1.0f), // Ignore  — muted red
        ImVec4(0.90f, 0.78f, 0.34f, 1.0f), // Overlap — amber
        ImVec4(0.45f, 0.80f, 0.48f, 1.0f), // Block   — green
    };
    const char* kResponseNames[3] = { "Ignore", "Overlap", "Block" };
}

void Plu::ProjectSettingsPanel::DrawPhysicsChannels()
{
    PLU_PROFILE_SCOPE("ProjectSettingsPanel::DrawPhysicsChannels");

    PhysicsChannelsManager* channels = PhysicsChannelsManager::GetInstance();

    ImGui::Spacing();
    if (!ImGui::CollapsingHeader(ICON_FA_LAYER_GROUP "  Physics Channels", ImGuiTreeNodeFlags_DefaultOpen))
        return;

    // Copy of the names — removing a channel mid-loop must not invalidate what we iterate.
    const DynamicArray<String> channelNames = channels->GetAllChannels();
    String channelToRemove;
    bool changed = false;

    const ImGuiTableFlags tableFlags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;
    if (ImGui::BeginTable("##physicsChannels", 5, tableFlags))
    {
        ImGui::TableSetupColumn("Channel", ImGuiTableColumnFlags_WidthStretch);
        for (const char* responseName : kResponseNames)
            ImGui::TableSetupColumn(responseName);
        ImGui::TableSetupColumn("##remove");

        // Custom header row so each response column is tinted like its radio buttons.
        ImGui::TableNextRow(ImGuiTableRowFlags_Headers);
        ImGui::TableSetColumnIndex(0);
        ImGui::TableHeader("Channel");
        for (int r = 0; r < 3; ++r)
        {
            ImGui::TableSetColumnIndex(r + 1);
            ImGui::PushStyleColor(ImGuiCol_Text, kResponseColors[r]);
            ImGui::TableHeader(kResponseNames[r]);
            ImGui::PopStyleColor();
        }
        ImGui::TableSetColumnIndex(4);
        ImGui::TableHeader("");

        for (UInt32 i = 0; i < channelNames.Size(); ++i)
        {
            PhysicsCollisionChannel* channel = channels->GetChannel(channelNames[i]);
            if (!channel)
                continue;

            const bool isDefault = channelNames[i] == "Default";

            ImGui::PushID(static_cast<int>(i));
            ImGui::TableNextRow();

            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(channel->Name.CStr());
            if (isDefault)
            {
                ImGui::SameLine();
                ImGui::TextDisabled("(fallback)");
            }

            const int response = static_cast<int>(channel->DefaultResponse);
            for (int r = 0; r < 3; ++r)
            {
                ImGui::TableSetColumnIndex(r + 1);
                ImGui::PushID(r);
                ImGui::PushStyleColor(ImGuiCol_CheckMark, kResponseColors[r]);
                if (ImGui::RadioButton("##response", response == r) && response != r)
                {
                    channel->DefaultResponse = static_cast<PhysicsCollisionResponse>(r);
                    changed = true;
                }
                ImGui::PopStyleColor();
                ImGui::PopID();
            }

            ImGui::TableSetColumnIndex(4);
            ImGui::BeginDisabled(isDefault);
            if (ImGui::SmallButton(ICON_FA_TRASH))
                channelToRemove = channelNames[i];
            ImGui::EndDisabled();
            if (isDefault)
                ImGui::SetItemTooltip("The Default channel cannot be removed.");

            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (!channelToRemove.IsEmpty())
    {
        channels->RemoveChannel(channelToRemove);
        changed = true;
    }

    // ---- Add channel ----
    const String newName = String(mNewChannelName).Strip();
    const bool nameTaken = channels->ChannelExists(newName);

    ImGui::SetNextItemWidth(200.0f);
    const bool submitted = ImGui::InputTextWithHint("##newChannelName", "New channel name", mNewChannelName,
                                                    sizeof(mNewChannelName), ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(110.0f);
    ImGui::Combo("##newChannelResponse", &mNewChannelResponse, kResponseNames, 3);
    ImGui::SameLine();

    const bool canAdd = !newName.IsEmpty() && !nameTaken;
    ImGui::BeginDisabled(!canAdd);
    if ((ImGui::Button(ICON_FA_PLUS " Add Channel") || submitted) && canAdd)
    {
        channels->AddChannel(newName, static_cast<PhysicsCollisionResponse>(mNewChannelResponse));
        mNewChannelName[0] = '\0';
        changed = true;
    }
    ImGui::EndDisabled();
    if (nameTaken)
    {
        ImGui::SameLine();
        ImGui::TextColored(kResponseColors[0], "Channel already exists");
    }

    ImGui::TextDisabled("A pair of channels uses the weaker of their responses (Ignore < Overlap < Block).");

    // Project config, not an asset — written straight to Config/PhysicsChannels.json. Opening another
    // project does not save the current one, so waiting for shutdown could lose the edit.
    if (changed)
        mEditorAppContext->EditorProjectManager->SavePhysicsChannels();
}
