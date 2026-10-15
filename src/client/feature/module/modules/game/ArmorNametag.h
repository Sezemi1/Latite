#pragma once

#include "client/feature/module/Module.h"

// Appends a one-line armor durability readout beneath other players' nametags.
//
// The module keeps no per-actor state: every line it adds starts with a unique
// marker (see ArmorNametag.cpp), so it can find and strip its own suffix from
// any nametag at any time, including when the module is disabled.
class ArmorNametag final : public Module {
public:
    ArmorNametag();
    ~ArmorNametag() override = default;

    void onUpdate(Event& event);
    void onDisable() override;

private:
    EnumData durabilityDisplayMode;
    ValueType maxDistance = FloatValue(32.f); // Blocks; 0 = unlimited.
};
