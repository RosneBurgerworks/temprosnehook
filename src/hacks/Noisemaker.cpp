/*
 * Noisemaker.cpp
 *
 *  Created on: Feb 2, 2017
 *      Author: nullifiedcat
 */

#include "common.hpp"
#include <settings/Bool.hpp>

namespace hacks::noisemaker
{
#if !ENABLE_TEXTMODE
static settings::Boolean enable{ "noisemaker-spam.enable", "false" };
#else
static settings::Boolean enable{ "noisemaker-spam.enable", "true" };
#endif

static Timer timer{};

static void CreateMove()
{
    if (enable && CE_GOOD(LOCAL_E))
    {
        if (timer.test_and_set(1000))
        {
            auto *kv = new KeyValues("+use_action_slot_item_server");
            g_IEngine->ServerCmdKeyValues(kv);
            auto *kv2 = new KeyValues("-use_action_slot_item_server");
            g_IEngine->ServerCmdKeyValues(kv2);
        }
    }
}

FnCommandCallbackVoid_t plus_use_action_slot_item_original;
FnCommandCallbackVoid_t minus_use_action_slot_item_original;

void plus_use_action_slot_item_hook()
{
    auto *kv = new KeyValues("+use_action_slot_item_server");
    g_IEngine->ServerCmdKeyValues(kv);
}

void minus_use_action_slot_item_hook()
{
    auto *kv = new KeyValues("-use_action_slot_item_server");
    g_IEngine->ServerCmdKeyValues(kv);
}

static void Init()
{
    auto plus  = g_ICvar->FindCommand("+use_action_slot_item");
    auto minus = g_ICvar->FindCommand("-use_action_slot_item");
    if (!plus || !minus)
        return ConColorMsg({ 255, 0, 0, 255 }, "Could not find +use_action_slot_item! INFINITE NOISE MAKER WILL NOT WORK!!!\n");
    plus_use_action_slot_item_original  = plus->m_fnCommandCallbackV1;
    minus_use_action_slot_item_original = minus->m_fnCommandCallbackV1;
    plus->m_fnCommandCallbackV1         = plus_use_action_slot_item_hook;
    minus->m_fnCommandCallbackV1        = minus_use_action_slot_item_hook;
}

static void Shutdown()
{
    auto plus  = g_ICvar->FindCommand("+use_action_slot_item");
    auto minus = g_ICvar->FindCommand("-use_action_slot_item");
    if (!plus || !minus)
        return ConColorMsg({ 255, 0, 0, 255 }, "Could not find +use_action_slot_item! INFINITE NOISE MAKER WILL NOT WORK!!!\n");
    plus->m_fnCommandCallbackV1  = plus_use_action_slot_item_original;
    minus->m_fnCommandCallbackV1 = minus_use_action_slot_item_original;
}
static InitRoutine EC(
    []()
    {
        Init();
        EC::Register(EC::CreateMove, CreateMove, "CM_Noisemaker", EC::average);
        EC::Register(EC::Shutdown, Shutdown, "SD_Noisemaker", EC::average);
    });
} // namespace hacks::noisemaker
