#include "common.hpp"

namespace hacks::antitaunts
{

#if ENABLE_TEXTMODE
static settings::Boolean remove_taunts{ "remove.taunts", "true" };
#else
static settings::Boolean remove_taunts{ "remove.taunts", "false" };
#endif

void CreateMove()
{
    for (int i = 1; i < g_GlobalVars->maxClients; ++i)
    {
        auto ent = ENTITY(i);
        if (CE_BAD(ent) || ent == LOCAL_E || ent->m_Type() != ENTITY_PLAYER)
            continue;
        RemoveCondition<TFCond_Taunting>(ent);
    }
}

static void register_remove_taunts(bool enable)
{
    if (enable)
        EC::Register(EC::CreateMove, CreateMove, "cm_antitaunt");
    else
        EC::Unregister(EC::CreateMove, "cm_antitaunt");
}

static InitRoutine init([]() {
    remove_taunts.installChangeCallback([](bool new_val) {
        register_remove_taunts(new_val);
    });
    if (*remove_taunts)
        register_remove_taunts(true);
});

} // namespace hacks::antitaunts
