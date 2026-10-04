//
// Created by Plutex on 10/4/26.
//

#include "PluEngine/Gameplay/Physics/PhysicsChannels.h"

#include <filesystem>

#include "PluEngine/Log.h"
#include "PluEngine/Timer.h"
#include "PluEngine/Core/DiskManager.h"

static Plu::PhysicsChannelsManager* gPhysicsChannelsManager = nullptr;

namespace
{
    constexpr UInt32 kChannelsJsonVersion = 1;

    // Binary layout: magic, version, channel count, then per channel: UInt8 response, name.
    constexpr UInt32 kChannelsBinaryMagic = 0x48434C50; // "PLCH"
    constexpr UInt32 kChannelsBinaryVersion = 2;
}

Plu::PhysicsChannelsManager::PhysicsChannelsManager()
{
    ResetToDefaults();
}

Plu::PhysicsChannelsManager::~PhysicsChannelsManager()
{
    for (PhysicsCollisionChannel* channel : mCollisionChannels) {
        delete channel;
    }
}

Plu::PhysicsChannelsManager * Plu::PhysicsChannelsManager::GetInstance()
{
    if (!gPhysicsChannelsManager) {
        gPhysicsChannelsManager = new PhysicsChannelsManager();
    }
    return gPhysicsChannelsManager;
}

void Plu::PhysicsChannelsManager::ResetToDefaults()
{
    for (PhysicsCollisionChannel* channel : mCollisionChannels) {
        delete channel;
    }
    mCollisionChannels.Clear();
    mFreeList.Clear();
    mChannelIdPerName.Clear();
    AddChannel("Default", PhysicsCollisionResponse::Block);
}

void Plu::PhysicsChannelsManager::AddChannel(const String &channelName, PhysicsCollisionResponse defaultResponse)
{
    if (channelName.Strip() == "") {
        PLU_CORE_ERROR("Invalid Channel Name!");
        return;
    }
    if (mChannelIdPerName.Contains(channelName)) {
        PLU_CORE_ERROR("Channel with that name already exists!");
        return;
    }
    PLU_CORE_ASSERT(!mFreeList.IsEmpty() || mCollisionChannels.Size() < kMaxChannels, "Too many physics channels!");

    PhysicsCollisionChannel* newChannel = new PhysicsCollisionChannel();
    newChannel->Name = channelName;
    newChannel->DefaultResponse = defaultResponse;

    UInt16 idx;
    if (mFreeList.IsEmpty()) {
        idx = static_cast<UInt16>(mCollisionChannels.Size());
        mCollisionChannels.PushBack(newChannel);
    } else {
        idx = mFreeList.Back();
        mFreeList.PopBack();
        mCollisionChannels[idx] = newChannel;
    }
    mChannelIdPerName[channelName] = idx;
    mRebuildChannelNames = true;
}

void Plu::PhysicsChannelsManager::RemoveChannel(const String &channelName)
{
    if (channelName == "Default") {
        PLU_CORE_ERROR("Cannot remove default physics channel!");
        return;
    }
    if (mChannelIdPerName.Contains(channelName)) {
        const UInt16 removedIdx = mChannelIdPerName[channelName];
        delete mCollisionChannels[removedIdx];
        mCollisionChannels[removedIdx] = nullptr;
        mFreeList.PushBack(removedIdx);
        mChannelIdPerName.Remove(channelName);
        mRebuildChannelNames = true;
    }
}

Plu::PhysicsCollisionChannel * Plu::PhysicsChannelsManager::GetChannel(String channelName)
{
    if (mChannelIdPerName.Contains(channelName)) {
        return mCollisionChannels[mChannelIdPerName[channelName]];
    }
    return nullptr;
}

Plu::PhysicsCollisionChannel * Plu::PhysicsChannelsManager::GetChannelById(UInt16 channelId) const
{
    if (channelId >= mCollisionChannels.Size()) {
        return nullptr;
    }
    return mCollisionChannels[channelId];
}

bool Plu::PhysicsChannelsManager::ChannelExists(const String &channelName) const
{
    return mChannelIdPerName.Contains(channelName);
}

UInt16 Plu::PhysicsChannelsManager::GetChannelId(PhysicsCollisionChannel *channel)
{
    if (!mChannelIdPerName.Contains(channel->Name)) {
        return kInvalidChannelId;
    }
    return mChannelIdPerName[channel->Name];
}

DynamicArray<Plu::String> Plu::PhysicsChannelsManager::GetAllChannels()
{
    static DynamicArray<Plu::String> allChannels;
    if (mRebuildChannelNames) {
        allChannels.Clear();
        for (auto channel : mCollisionChannels) {
            if (channel) {
                allChannels.PushBack(channel->Name);
            }
        }
        mRebuildChannelNames = false;
    }
    return allChannels;
}

bool Plu::PhysicsChannelsManager::CanBeCompletelyIgnored(UInt16 channelA, UInt16 channelB)
{
    const PhysicsCollisionChannel* a = GetChannelById(channelA >> 1);
    const PhysicsCollisionChannel* b = GetChannelById(channelB >> 1);

    if (a == nullptr || b == nullptr) {
        return false;
    }

    return a->DefaultResponse == PhysicsCollisionResponse::Ignore ||
        b->DefaultResponse == PhysicsCollisionResponse::Ignore;
}

bool Plu::PhysicsChannelsManager::CanBlock(UInt16 channelA, UInt16 channelB)
{
    // A layer whose channel was removed falls back to Default.
    const PhysicsCollisionChannel* defaultChannel = mCollisionChannels[kDefaultChannelId];
    const PhysicsCollisionChannel* a = GetChannelById(channelA >> 1);
    const PhysicsCollisionChannel* b = GetChannelById(channelB >> 1);
    if (a == nullptr) a = defaultChannel;
    if (b == nullptr) b = defaultChannel;

    return std::min(a->DefaultResponse, b->DefaultResponse) == PhysicsCollisionResponse::Block;
}

void Plu::PhysicsChannelsManager::RestoreChannel(const String &channelName, PhysicsCollisionResponse defaultResponse)
{
    if (channelName == "Default") {
        // Default always exists, only its response is project data.
        mCollisionChannels[kDefaultChannelId]->DefaultResponse = defaultResponse;
        return;
    }
    if (mChannelIdPerName.Contains(channelName)) {
        PLU_CORE_WARN("Skipping duplicate physics channel '{}'", channelName.CStr());
        return;
    }
    AddChannel(channelName, defaultResponse);
}

nlohmann::json Plu::PhysicsChannelsManager::SaveToJson() const
{
    nlohmann::json channels = nlohmann::json::array();
    for (PhysicsCollisionChannel* channel : mCollisionChannels) {
        if (!channel) {
            continue;
        }
        nlohmann::json entry;
        entry["Name"] = channel->Name.CStr();
        entry["DefaultResponse"] = TypeSerializer<PhysicsCollisionResponse>::Serialize(&channel->DefaultResponse);
        channels.push_back(entry);
    }

    nlohmann::json json;
    json["Version"] = kChannelsJsonVersion;
    json["Channels"] = channels;
    return json;
}

bool Plu::PhysicsChannelsManager::LoadFromJson(const nlohmann::json &json)
{
    ResetToDefaults();
    if (!json.is_object() || !json.contains("Channels") || !json["Channels"].is_array()) {
        PLU_CORE_ERROR("Invalid physics channels JSON, resetting to defaults");
        return false;
    }

    for (const nlohmann::json& entry : json["Channels"]) {
        if (!entry.is_object() || !entry.contains("Name") || !entry["Name"].is_string()) {
            PLU_CORE_ERROR("Skipping physics channel entry without a name");
            continue;
        }
        PhysicsCollisionResponse response = PhysicsCollisionResponse::Block;
        if (entry.contains("DefaultResponse")) {
            // nullptr context: an enum holds no asset/object references.
            TypeSerializer<PhysicsCollisionResponse>::Deserialize(nullptr, entry["DefaultResponse"], &response);
        }
        RestoreChannel(entry["Name"].get<std::string>().c_str(), response);
    }
    return true;
}

bool Plu::PhysicsChannelsManager::SaveToJsonFile(const PathW &path) const
{
    PLU_PROFILE_SCOPE("PhysicsChannelsManager::SaveToJsonFile");
    return DiskManager::SaveJson(path.ToString(), SaveToJson());
}

bool Plu::PhysicsChannelsManager::LoadFromJsonFile(const PathW &path)
{
    PLU_PROFILE_SCOPE("PhysicsChannelsManager::LoadFromJsonFile");
    if (!std::filesystem::exists(path.CStr())) {
        ResetToDefaults();
        return false;
    }
    std::optional<nlohmann::json> json = DiskManager::LoadJson(path);
    if (!json.has_value()) {
        PLU_CORE_ERROR("Failed to read physics channels from {}", path.ToString().ToNarrow().CStr());
        ResetToDefaults();
        return false;
    }
    return LoadFromJson(json.value());
}

bool Plu::PhysicsChannelsManager::SaveToBinaryFile(const PathW &path) const
{
    PLU_PROFILE_SCOPE("PhysicsChannelsManager::SaveToBinaryFile");
    BinaryFileWriter writer(path);
    if (!writer.IsOpen()) {
        PLU_CORE_ERROR("Failed to write physics channels: {}", writer.GetLastError().CStr());
        return false;
    }

    UInt32 channelCount = 0;
    for (const PhysicsCollisionChannel* channel : mCollisionChannels) {
        if (channel) ++channelCount;
    }
    writer.Write(kChannelsBinaryMagic);
    writer.Write(kChannelsBinaryVersion);
    writer.Write(channelCount);
    for (const PhysicsCollisionChannel* channel : mCollisionChannels) {
        if (!channel) {
            continue;
        }
        writer.Write(static_cast<UInt8>(channel->DefaultResponse));
        writer.WriteString(channel->Name);
    }

    if (!writer.CloseFile()) {
        PLU_CORE_ERROR("Failed to write physics channels: {}", writer.GetLastError().CStr());
        return false;
    }
    return true;
}

bool Plu::PhysicsChannelsManager::LoadFromBinaryFile(const PathW &path)
{
    PLU_PROFILE_SCOPE("PhysicsChannelsManager::LoadFromBinaryFile");
    ResetToDefaults();
    if (!std::filesystem::exists(path.CStr())) {
        return false;
    }
    BinaryFileReader reader(path);
    if (!reader.IsOpen()) {
        PLU_CORE_ERROR("Failed to read physics channels: {}", reader.GetLastError().CStr());
        return false;
    }

    UInt32 magic = 0, version = 0, channelCount = 0;
    reader.Read(magic);
    reader.Read(version);
    reader.Read(channelCount);
    if (reader.HasError() || magic != kChannelsBinaryMagic || version != kChannelsBinaryVersion) {
        PLU_CORE_ERROR("Invalid physics channels file {} (magic {:#x}, version {})", path.ToString().ToNarrow().CStr(), magic, version);
        return false;
    }

    for (UInt32 i = 0; i < channelCount; ++i) {
        UInt8 response = 0;
        String name;
        reader.Read(response);
        reader.ReadString(name);
        if (reader.HasError()) {
            PLU_CORE_ERROR("Physics channels file {} is truncated: {}", path.ToString().ToNarrow().CStr(), reader.GetLastError().CStr());
            ResetToDefaults();
            return false;
        }
        if (response > static_cast<UInt8>(PhysicsCollisionResponse::Block)) {
            PLU_CORE_WARN("Physics channel '{}' has unknown response {}, using Block", name.CStr(), response);
            response = static_cast<UInt8>(PhysicsCollisionResponse::Block);
        }
        RestoreChannel(name, static_cast<PhysicsCollisionResponse>(response));
    }
    return true;
}
