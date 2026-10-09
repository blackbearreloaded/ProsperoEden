// SPDX-License-Identifier: GPL-3.0-or-later
// The guest controller applet (a game's "connect controllers" screen) without a UI. Eden's default
// connects only the game's minimum player count, which disconnects a second player whenever a game
// asks; this connects one player per PS5 controller in use, within the game's limits, with the
// same controller style priority (Pro Controller, dual Joy-Con, single Joy-Con, handheld).
#pragma once
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdio>
#include <optional>
#include "common/settings.h"
#include "core/frontend/applets/controller.h"
#include "devices.h"
#include "diagnostics.h"
#include "hid_core/frontend/emulated_controller.h"
#include "hid_core/hid_core.h"

namespace Eden {
// The controller a player gets for what the game allows: a Pro Controller, else a Joy-Con pair,
// else single Joy-Cons (a left one for players 1 and 3 and a right one for 2 and 4 when the game
// takes both), else the handheld for player 1. A game that takes only the handheld gets it in
// Docked mode too, and a game that names none of these gets a Pro Controller: an answer the game
// may refuse (it then shows its screen again), where no answer left it waiting for ever. Nothing
// only for the players after the first of a handheld-only game.
//
// A game's own Controller type (Library > Game settings > Controls, settings_store.h
// kControllerKeys) goes first when the game takes it: some games take a Pro Controller and then
// do not work with it. -1 is no choice. Set when a session starts.
inline std::atomic<int> session_controller{-1};

inline std::optional<Core::HID::NpadStyleIndex> ChosenStyle(int controller) {
    using Core::HID::NpadStyleIndex;
    static constexpr NpadStyleIndex kStyles[] = {NpadStyleIndex::Fullkey, NpadStyleIndex::Handheld,
                                                 NpadStyleIndex::JoyconDual, NpadStyleIndex::JoyconLeft,
                                                 NpadStyleIndex::JoyconRight};
    if (controller < 0 || controller >= static_cast<int>(std::size(kStyles))) return std::nullopt;
    return kStyles[controller];
}

// The same choice as Eden's setting for a player, for the start of a session (a Pro Controller
// without one).
inline ::Settings::ControllerType ControllerSetting(int controller) {
    using Type = ::Settings::ControllerType;
    static constexpr Type kTypes[] = {Type::ProController, Type::Handheld, Type::DualJoyconDetached,
                                      Type::LeftJoycon, Type::RightJoycon};
    return controller < 0 || controller >= static_cast<int>(std::size(kTypes)) ? Type::ProController : kTypes[controller];
}

inline bool TakesStyle(const Core::Frontend::ControllerParameters& parameters, Core::HID::NpadStyleIndex style) {
    using Core::HID::NpadStyleIndex;
    return style == NpadStyleIndex::Fullkey ? parameters.allow_pro_controller :
           style == NpadStyleIndex::Handheld ? parameters.allow_handheld :
           style == NpadStyleIndex::JoyconDual ? parameters.allow_dual_joycons :
           style == NpadStyleIndex::JoyconLeft ? parameters.allow_left_joycon :
           style == NpadStyleIndex::JoyconRight && parameters.allow_right_joycon;
}

inline std::optional<Core::HID::NpadStyleIndex> ControllerStyle(
    const Core::Frontend::ControllerParameters& parameters, std::size_t index, int controller = -1) {
    using Core::HID::NpadStyleIndex;
    if (const auto chosen = ChosenStyle(controller); chosen && TakesStyle(parameters, *chosen))
        return *chosen != NpadStyleIndex::Handheld || index == 0 ? chosen : std::nullopt;
    if (parameters.allow_pro_controller) return NpadStyleIndex::Fullkey;
    if (parameters.allow_dual_joycons) return NpadStyleIndex::JoyconDual;
    if (parameters.allow_left_joycon && parameters.allow_right_joycon)
        return index % 2 == 0 ? NpadStyleIndex::JoyconLeft : NpadStyleIndex::JoyconRight;
    if (parameters.allow_left_joycon) return NpadStyleIndex::JoyconLeft;
    if (parameters.allow_right_joycon) return NpadStyleIndex::JoyconRight;
    if (parameters.allow_handheld) return index == 0 ? std::optional{NpadStyleIndex::Handheld} : std::nullopt;
    return NpadStyleIndex::Fullkey;
}

class PadControllerApplet final : public Core::Frontend::ControllerApplet {
public:
    PadControllerApplet(Core::HID::HIDCore& hid_core_, const Pad& pad_) : hid_core{hid_core_}, pad{pad_} {}

    void Close() const override {}

    void ReconfigureControllers(ReconfigureCallback callback,
                                const Core::Frontend::ControllerParameters& parameters) const override {
        const std::size_t min_players =
            parameters.enable_single_mode ? 1 : std::max<std::size_t>(std::max<int>(parameters.min_players, 1), 1);
        const std::size_t max_players =
            parameters.enable_single_mode ? 1 : std::max<std::size_t>(std::max<int>(parameters.max_players, 1), min_players);
        const std::size_t pads = std::popcount(pad.ConnectedPlayers() | 1u);
        const std::size_t players = std::clamp(pads, min_players, max_players);
        const bool docked = ::Settings::IsDockedMode();
        std::size_t connected = 0;
        bool handheld_given = false;
        const int chosen = session_controller.load();
        // The handheld is a controller of its own, not player 1 with another style; player 1's
        // DualSense plays it (pad.cpp).
        auto* handheld = hid_core.GetEmulatedController(Core::HID::NpadIdType::Handheld);
        handheld->Disconnect();
        for (std::size_t index = 0; index < Core::HID::HIDCore::available_controllers - 2; ++index) {
            auto* controller = hid_core.GetEmulatedControllerByIndex(index);
            controller->Disconnect();
            if (index >= players) continue;
            const auto style = ControllerStyle(parameters, index, chosen);
            if (!style) continue;
            auto* target = *style == Core::HID::NpadStyleIndex::Handheld ? handheld : controller;
            handheld_given |= target == handheld;
            // Player 1's DualSense plays the handheld from here on, and stops when a later screen
            // gives the game another controller.
            if (index == 0) handheld_in_use = target == handheld;
            target->SetNpadStyleIndex(*style);
            target->Connect(true);
            ++connected;
        }
        if (!handheld_given) handheld_in_use = false;
        if (const auto style = ChosenStyle(chosen); style && !TakesStyle(parameters, *style))
            Report("controllers", "This game does not take the Controller type chosen for it: it gets what it takes");
        // What the game asked for goes to the log: a game stuck on this screen can then be told
        // apart from one that asks for a controller this port does not provide.
        char line[224];
        std::snprintf(line, sizeof(line),
                      "Game asks for %d to %d players (single mode %d); takes pro=%d pair=%d left=%d right=%d "
                      "handheld=%d; %s, %zu PS5 controller(s): connected %zu",
                      parameters.min_players, parameters.max_players, parameters.enable_single_mode,
                      parameters.allow_pro_controller, parameters.allow_dual_joycons, parameters.allow_left_joycon,
                      parameters.allow_right_joycon, parameters.allow_handheld, docked ? "docked" : "handheld",
                      pads, connected);
        Report("controllers", line);
        const bool named = parameters.allow_pro_controller || parameters.allow_dual_joycons ||
                           parameters.allow_left_joycon || parameters.allow_right_joycon;
        if (!named && parameters.allow_handheld && docked)
            Report("controllers", "This game only takes the handheld controller: it gets one in Docked mode "
                                  "too. If it refuses it, set its Console mode to Handheld in the Library");
        else if (!named && !parameters.allow_handheld)
            Report("controllers", "This game names no controller that ProsperoEden provides: it gets a Pro Controller");
        callback(true);
    }

private:
    Core::HID::HIDCore& hid_core;
    const Pad& pad;
};
}
