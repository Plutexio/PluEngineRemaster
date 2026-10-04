//
// Created by Plutex on 10/4/26.
//

#ifndef PLUENGINE_PHYSICSCHANNELS_H
#define PLUENGINE_PHYSICSCHANNELS_H

#include <limits>

#include "PluEngine/Core.h"
#include "PluSTL_FWD.h"
#include "PhysicsChannels.generated.h"

namespace Plu
{
    PLU_ENUM(PyNamespace=Plu)
    enum class PhysicsCollisionResponse
    {
        Ignore,
        Overlap,
        Block
    };

    PLU_STRUCT()
    struct PhysicsCollisionChannel
    {
        REFLECTION_BODY_PHYSICSCOLLISIONCHANNEL()
    public:
        PhysicsCollisionResponse DefaultResponse = PhysicsCollisionResponse::Block;
        String Name;
    };

    // Owns the project's physics channels. A channel's id is its slot index and stays fixed for the
    // channel's whole life — bodies bake it into their Jolt ObjectLayer (id << 1 | moving), so ids must
    // not shift when another channel is removed. A removed channel leaves an empty slot that goes on a
    // free list and is handed out to the next added channel (same scheme as EngineObjectManager).
    // Ids are runtime-only: everything persisted refers to channels by name.
    class PLUGAMEPLAY_API PhysicsChannelsManager
    {
        DynamicArray<PhysicsCollisionChannel*> mCollisionChannels; // nullptr = free slot
        DynamicArray<UInt16> mFreeList;
        HashMap<String, UInt16> mChannelIdPerName;

        bool mRebuildChannelNames = true;

        void ResetToDefaults();
        // Re-adds one loaded channel; "Default" only takes the response, duplicates are skipped.
        void RestoreChannel(const String& channelName, PhysicsCollisionResponse defaultResponse);
    public:
        static constexpr UInt16 kDefaultChannelId = 0;
        static constexpr UInt16 kInvalidChannelId = std::numeric_limits<UInt16>::max();
        // The lowest ObjectLayer bit is the moving flag, so ids have 15 bits.
        static constexpr UInt16 kMaxChannels = std::numeric_limits<UInt16>::max() / 2;

        PhysicsChannelsManager();
        ~PhysicsChannelsManager();

        static PhysicsChannelsManager* GetInstance();

        void AddChannel(const String &channelName, PhysicsCollisionResponse defaultResponse = PhysicsCollisionResponse::Block);
        void RemoveChannel(const String &channelName);

        PhysicsCollisionChannel* GetChannel(String channelName);
        // Null for an id that is out of range or belongs to a removed channel.
        [[nodiscard]] PhysicsCollisionChannel* GetChannelById(UInt16 channelId) const;
        [[nodiscard]] bool ChannelExists(const String &channelName) const;
        UInt16 GetChannelId(PhysicsCollisionChannel* channel);

        // Names of the live channels, in id order.
        DynamicArray<String> GetAllChannels();

        bool CanBeCompletelyIgnored(UInt16 channelA, UInt16 channelB);

        //If true then block if false we overlap
        bool CanBlock(UInt16 channelA, UInt16 channelB);

        // Persistence (name + default response per channel, no ids). The editor keeps channels as JSON
        // in the project's Config/ directory, a shipped build reads the binary file written next to the
        // runtime. Loading replaces every channel; a missing file resets to just "Default" and returns false.
        [[nodiscard]] nlohmann::json SaveToJson() const;
        bool LoadFromJson(const nlohmann::json& json);
        bool SaveToJsonFile(const PathW& path) const;
        bool LoadFromJsonFile(const PathW& path);
        bool SaveToBinaryFile(const PathW& path) const;
        bool LoadFromBinaryFile(const PathW& path);
    };

    // A channel is serialized as just its name — the channel's data lives in PhysicsChannelsManager.
    // Deserialization resolves the name against the manager and falls back to "Default" when the
    // channel does not exist (renamed or removed since the data was saved).
    template <>
    struct TypeSerializer<PhysicsCollisionChannel>
    {
        static nlohmann::json Serialize(void* value)
        {
            return static_cast<PhysicsCollisionChannel*>(value)->Name.CStr();
        }

        static void Deserialize(DeserializationContext*, const nlohmann::json& json, void* outValue)
        {
            PhysicsCollisionChannel* channel = static_cast<PhysicsCollisionChannel*>(outValue);
            PhysicsChannelsManager* channels = PhysicsChannelsManager::GetInstance();
            if (!json.is_string()) {
                PLU_CORE_ERROR("PhysicsCollisionChannel JSON is not a string, using Default");
                *channel = *channels->GetChannel("Default");
                return;
            }
            const String name = json.get<std::string>().c_str();
            if (PhysicsCollisionChannel* existing = channels->GetChannel(name)) {
                *channel = *existing;
                return;
            }
            PLU_CORE_ERROR("Physics channel '{}' does not exist, using Default", name.CStr());
            *channel = *channels->GetChannel("Default");
        }

        // Dropdown of the project's channels.
        static bool EditorControl(void* value, const String& name)
        {
            PhysicsCollisionChannel* channel = static_cast<PhysicsCollisionChannel*>(value);
            PhysicsChannelsManager* channels = PhysicsChannelsManager::GetInstance();
            bool changed = false;
            if (ImGui::BeginCombo(name.CStr(), channel->Name.CStr())) {
                for (const String& channelName : channels->GetAllChannels()) {
                    const bool selected = channelName == channel->Name;
                    if (ImGui::Selectable(channelName.CStr(), selected) && !selected) {
                        *channel = *channels->GetChannel(channelName);
                        changed = true;
                    }
                    if (selected)
                        ImGui::SetItemDefaultFocus();
                }
                ImGui::EndCombo();
            }
            return changed;
        }
    };

}

#endif //PLUENGINE_PHYSICSCHANNELS_H
