#include "pch.h"
#include "ArmorNametag.h"

#include "client/event/events/UpdateEvent.h"
#include "mc/Addresses.h"
#include "mc/common/client/game/ClientInstance.h"
#include "mc/common/world/Minecraft.h"
#include "mc/common/entity/component/ActorEquipmentComponent.h"
#include "mc/common/world/Item.h"
#include "mc/common/world/ItemStack.h"
#include "mc/common/world/ItemStackBase.h"
#include "mc/common/world/actor/Actor.h"
#include "mc/common/world/level/Level.h"

#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace {
enum class DisplayMode : int { Percentage = 0, Points = 1 };

// "\302\247" is the UTF-8 encoding of the section sign. Octal escapes stop after
// three digits, so a color code can follow directly without splitting the literal.
constexpr std::string_view kSectionSign = "\302\247";

// Everything this module appends begins with this marker ("\n§8[§7"). It is the
// single source of truth for both building and stripping the line.
constexpr std::string_view kArmorMarker = "\n\302\2478[\302\2477";

constexpr std::array<char, 4> kSlotLabels{'H', 'C', 'L', 'B'}; // helmet, chest, legs, boots
constexpr int kGreenAtOrAbove = 60;
constexpr int kYellowAtOrAbove = 25;

void addColor(std::string& text, char color) {
    text += kSectionSign;
    text += color;
}

struct ArmorDurability {
    int remaining;
    int percent;
};

std::optional<ArmorDurability> getArmorDurability(SDK::Actor* actor, int armorSlot) {
    // Actor::getArmor() dereferences the equipment component internally, so guard it first.
    auto* equipment = actor->tryGetComponent<SDK::ActorEquipmentComponent>();
    if (!equipment || !equipment->armorContainer) return std::nullopt;

    auto* stack = actor->getArmor(armorSlot);
    if (!stack) return std::nullopt;

    auto* item = stack->getItem();
    if (!item) return std::nullopt; // Empty armor slot.

    const int maxDamage = item->getMaxDamage();
    if (maxDamage <= 0) return std::nullopt; // Item has no durability value.

    const int damage = static_cast<int>(stack->getDamageValue());
    const int remaining = std::clamp(maxDamage - damage, 0, maxDamage);
    // Integer division rounds down, so a slightly damaged piece never reads "100%".
    const int percent = remaining * 100 / maxDamage;
    return ArmorDurability{remaining, percent};
}

std::string makeArmorLine(SDK::Actor* actor, DisplayMode mode) {
    std::string text(kArmorMarker);
    text.reserve(48);

    for (int slot = 0; slot < static_cast<int>(kSlotLabels.size()); ++slot) {
        if (slot != 0) text += ' ';

        const auto durability = getArmorDurability(actor, slot);
        if (!durability) {
            addColor(text, '7');
            text += kSlotLabels[slot];
            text += "--";
            continue;
        }

        addColor(text, durability->percent >= kGreenAtOrAbove    ? 'a'
                       : durability->percent >= kYellowAtOrAbove ? 'e'
                                                                 : 'c');
        text += kSlotLabels[slot];
        if (mode == DisplayMode::Points) {
            text += std::to_string(durability->remaining);
        } else {
            text += std::to_string(durability->percent);
            text += '%';
        }
    }

    addColor(text, '8');
    text += ']';
    return text;
}

// Returns the nametag without our suffix. The view points into `tag`.
std::string_view stripArmorLine(std::string_view tag) {
    const auto marker = tag.find(kArmorMarker);
    return marker == std::string_view::npos ? tag : tag.substr(0, marker);
}

// These signatures are resolved by Latite at startup. Avoid calling SDK wrappers if one failed.
bool nameTagApiReady() {
    return Signatures::Actor_getNameTag.result && Signatures::Actor_setNameTag.result;
}

// Reads a float slider, tolerating a config that holds some other value type.
float getFloatOrDefault(ValueType const& value, float fallback) {
    if (!std::holds_alternative<FloatValue>(value)) return fallback;
    return std::get<FloatValue>(value).value;
}

auto* currentLevel() {
    auto* client = SDK::ClientInstance::get();
    return client && client->minecraft ? client->minecraft->getLevel() : nullptr;
}
} // namespace

ArmorNametag::ArmorNametag()
    : Module("ArmorNametag", LocalizeString::get("client.module.armorNametag.name"),
             LocalizeString::get("client.module.armorNametag.desc"), GAME) {
    durabilityDisplayMode.addEntry({static_cast<int>(DisplayMode::Percentage),
                                    LocalizeString::get("client.module.armorNametag.displayMode.percentage.name"),
                                    LocalizeString::get("client.module.armorNametag.displayMode.percentage.desc")});
    durabilityDisplayMode.addEntry({static_cast<int>(DisplayMode::Points),
                                    LocalizeString::get("client.module.armorNametag.displayMode.points.name"),
                                    LocalizeString::get("client.module.armorNametag.displayMode.points.desc")});
    addEnumSetting("displayMode", LocalizeString::get("client.module.armorNametag.displayMode.name"),
                   LocalizeString::get("client.module.armorNametag.displayMode.desc"), durabilityDisplayMode);
    addSliderSetting("maxDistance", LocalizeString::get("client.module.armorNametag.maxDistance.name"),
                     LocalizeString::get("client.module.armorNametag.maxDistance.desc"), maxDistance,
                     FloatValue(0.f), FloatValue(128.f), FloatValue(1.f));

    listen<UpdateEvent>((EventListenerFunc)&ArmorNametag::onUpdate);
}

void ArmorNametag::onUpdate(Event&) {
    if (!nameTagApiReady() || !Signatures::ItemStackBase_getDamageValue.result) return;

    auto* client = SDK::ClientInstance::get();
    auto* level = currentLevel();
    auto* localPlayer = client ? client->getLocalPlayer() : nullptr;
    if (!level || !localPlayer) return;

    const auto mode = static_cast<DisplayMode>(durabilityDisplayMode.getSelectedKey());
    const float maxDist = getFloatOrDefault(maxDistance, 32.f); // <= 0 means unlimited.
    const auto localPos = localPlayer->getPos();

    for (SDK::Actor* actor : level->getRuntimeActorList()) {
        if (!actor || actor == localPlayer || !actor->isPlayer()) continue;

        const std::string currentTag = actor->getNameTag();
        const std::string_view baseTag = stripArmorLine(currentTag);

        // Re-derive from the live tag every update, so legitimate name changes
        // from the server are picked up and our line is never stacked twice.
        std::string desiredTag(baseTag);

        // Don't turn a deliberately empty (hidden) nametag into a visible one,
        // don't leave a frozen readout on players who became invisible, and drop
        // the line once a player is farther away than the configured range.
        const bool inRange = maxDist <= 0.f || actor->getPos().distance(localPos) <= maxDist;
        if (!baseTag.empty() && !actor->isInvisible() && inRange) {
            desiredTag += makeArmorLine(actor, mode);
        }

        if (desiredTag != currentTag) {
            actor->setNameTag(&desiredTag);
        }
    }
}

void ArmorNametag::onDisable() {
    auto* level = currentLevel();
    if (!level || !nameTagApiReady()) return;

    for (SDK::Actor* actor : level->getRuntimeActorList()) {
        if (!actor || !actor->isPlayer()) continue;

        const std::string currentTag = actor->getNameTag();
        const std::string_view baseTag = stripArmorLine(currentTag);
        if (baseTag.size() == currentTag.size()) continue; // Not ours / nothing to remove.

        std::string restoredTag(baseTag);
        actor->setNameTag(&restoredTag);
    }
}
