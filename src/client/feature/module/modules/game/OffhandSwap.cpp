#include "pch.h"
#include "OffhandSwap.h"

#include <mc/common/network/packet/CommandRequestPacket.h>

#include "mc/common/network/MinecraftPackets.h"

#include <string>
#include <variant>

namespace {
// Reads a float setting, tolerating a config that holds some other value type.
float getFloatOrDefault(ValueType const& value, float fallback) {
    if (!std::holds_alternative<FloatValue>(value)) return fallback;
    return std::get<FloatValue>(value).value;
}
} // namespace

OffhandSwap::OffhandSwap()
    : Module("OffhandSwap", LocalizeString::get("client.module.offhandSwap.name"),
             LocalizeString::get("client.module.offhandSwap.desc"), GAME, nokeybind) {
    addSetting("swapKey", LocalizeString::get("client.module.offhandSwap.swapKey.name"),
               LocalizeString::get("client.module.offhandSwap.swapKey.desc"), this->swapKey);
    addSetting("swapCommand", LocalizeString::get("client.module.offhandSwap.swapCommand.name"),
               LocalizeString::get("client.module.offhandSwap.swapCommand.desc"), this->swapCommand);
    addSliderSetting("cooldown", LocalizeString::get("client.module.offhandSwap.cooldown.name"),
                     LocalizeString::get("client.module.offhandSwap.cooldown.desc"), this->cooldownMs,
                     FloatValue(50.f), FloatValue(2000.f), FloatValue(50.f));
    addSetting("blockKey", LocalizeString::get("client.module.offhandSwap.blockKey.name"),
               LocalizeString::get("client.module.offhandSwap.blockKey.desc"), this->blockKey);

    listen<KeyUpdateEvent>(static_cast<EventListenerFunc>(&OffhandSwap::onKey));
}

void OffhandSwap::onKey(Event& evG) {
    auto& ev = reinterpret_cast<KeyUpdateEvent&>(evG);

    // Ignore other keys, and never touch key presses while a menu or chat is open.
    if (ev.getKey() != std::get<KeyValue>(this->swapKey).value || ev.inUI()) return;

    // Swallow both press and release so the game never sees a stray half of the key.
    if (std::get<BoolValue>(this->blockKey)) ev.setCancelled(true);

    if (!ev.isDown()) return;

    const auto now = std::chrono::steady_clock::now();
    const auto cooldown = std::chrono::milliseconds(static_cast<int>(getFloatOrDefault(this->cooldownMs, 250.f)));
    if (now - lastSwap < cooldown) return;

    // Accept "bf:swap" or "/bf:swap" in the setting.
    std::string command = util::WStrToStr(std::get<TextValue>(this->swapCommand).str);
    command.erase(0, command.find_first_not_of(" /"));
    if (command.empty() || command.size() > 200) return;

    auto* client = SDK::ClientInstance::get();
    auto* player = client ? client->getLocalPlayer() : nullptr;
    if (!player || !player->packetSender) return;

    auto pkt = SDK::MinecraftPackets::createPacket(SDK::PacketID::COMMAND_REQUEST);
    auto* cmd = reinterpret_cast<SDK::CommandRequestPacket*>(pkt.get());
    cmd->applyCommand("/" + command);
    player->packetSender->sendToServer(pkt.get());

    lastSwap = now;
}
