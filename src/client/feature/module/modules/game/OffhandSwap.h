#pragma once

#include <client/feature/module/Module.h>

#include <chrono>

// Press a key to swap main hand <-> offhand, like Java's F key.
//
// Latite has no safe way to move items itself, so this sends a command to the server
// (default "bf:swap", provided by the Java PvP add-on) when the key is pressed.
// It works wherever the server offers such a command.
class OffhandSwap : public Module {
public:
    OffhandSwap();
    virtual ~OffhandSwap() {};

    void onKey(Event& ev);

private:
    ValueType swapKey = KeyValue('F');
    ValueType swapCommand = TextValue(L"bf:swap");
    ValueType cooldownMs = FloatValue(250.f);
    ValueType blockKey = BoolValue(true);

    std::chrono::steady_clock::time_point lastSwap{};
};
