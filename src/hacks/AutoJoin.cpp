/*
 * AutoJoin.cpp
 *
 *  Created on: Jul 28, 2017
 *      Author: nullifiedcat
 */

#include <settings/Int.hpp>
#include "HookTools.hpp"
#include <hacks/AutoJoin.hpp>

#include "common.hpp"
#include "hack.hpp"
#include "MiscTemporary.hpp"

namespace hacks::autojoin
{
static settings::Boolean autojoin_team{ "autojoin.team", "true" };
static settings::Boolean random_class{ "autojoin.random-class", "false" };
static settings::Int autojoin_class{ "autojoin.class", "0" };
static settings::Boolean auto_queue{ "autojoin.auto-queue", "false" };
static settings::Boolean auto_requeue{ "autojoin.auto-requeue", "false" };
static settings::Boolean partybypass{ "hack.party-bypass", "true" };

/*
 * Credits to Blackfire for helping me with auto-requeue!
 */

const std::string class_names[] = { "scout", "sniper", "soldier", "demoman", "medic", "heavyweapons", "pyro", "spy", "engineer" };

bool UnassignedTeam()
{
    return !g_pLocalPlayer->team || g_pLocalPlayer->team == TEAM_SPEC;
}

bool UnassignedClass()
{
    return g_pLocalPlayer->clazz != *autojoin_class;
}

static Timer startqueue_timer{};
#if !ENABLE_VISUALS
static Timer queue_timer{};
#endif

void UpdateSearch()
{
    if (!*auto_queue && !*auto_requeue || g_IEngine->IsInGame())
    {
#if !ENABLE_VISUALS
        queue_timer.update();
#endif
        return;
    }

    static uintptr_t addr    = CSignature::GetClientSignature("C7 04 24 ? ? ? ? 8D 7D ? 31 F6");
    static auto offset0      = uintptr_t(*(uintptr_t *) (addr + 0x3));
    static uintptr_t offset1 = CSignature::GetClientSignature("55 89 E5 83 EC ? 8B 45 ? 8B 80 ? ? ? ? 85 C0 74 ? C7 44 24 ? ? ? ? ? 89 04 24 E8 ? ? ? ? 85 C0 74 ? 8B 40");
    typedef int (*GetPendingInvites_t)(uintptr_t);
    auto GetPendingInvites = GetPendingInvites_t(offset1);
    int invites            = GetPendingInvites(offset0);

    re::CTFGCClientSystem *gc = re::CTFGCClientSystem::GTFGCClientSystem();
    re::CTFPartyClient *pc    = re::CTFPartyClient::GTFPartyClient();

    if (current_user_cmd && gc && gc->BConnectedToMatchServer(false) && gc->BHaveLiveMatch())
    {
#if !ENABLE_VISUALS
        queue_timer.update();
#endif
        tfmm::LeaveQueue();
    }
    //    if (gc && !gc->BConnectedToMatchServer(false) &&
    //            queuetime.test_and_set(10 * 1000 * 60) &&
    //            !gc->BHaveLiveMatch())
    //        tfmm::LeaveQueue();

    if (*auto_requeue && startqueue_timer.check(5000) && gc && !gc->BConnectedToMatchServer(false) && !gc->BHaveLiveMatch() && !invites && pc && !(pc->BInQueueForMatchGroup(tfmm::GetQueue()) || pc->BInQueueForStandby()))
    {
        logging::Info("Starting queue for standby, Invites %d", invites);
        tfmm::StartQueueStandby();
    }

    if (*auto_queue && startqueue_timer.check(5000) && gc && !gc->BConnectedToMatchServer(false) && !gc->BHaveLiveMatch() && !invites && pc && !(pc->BInQueueForMatchGroup(tfmm::GetQueue()) || pc->BInQueueForStandby()))
    {
        logging::Info("Starting queue, Invites %d", invites);
        tfmm::StartQueue();
    }
    startqueue_timer.test_and_set(5000);
#if !ENABLE_VISUALS
    if (queue_timer.test_and_set(600000))
        g_IEngine->ClientCmd_Unrestricted("quit"); // lol
#endif
}

static void Update()
{
    static Timer join_timer{};
    
    if (join_timer.test_and_set(500))
    {
        if (*autojoin_team && UnassignedTeam())
            hack::ExecuteCommand("autoteam");
        else if (*autojoin_class && UnassignedClass() && *autojoin_class < 10)
            g_IEngine->ExecuteClientCmd(format("join_class ", class_names[*autojoin_class - 1]).c_str());
        else if (*random_class && UnassignedClass())
            g_IEngine->ExecuteClientCmd(format("join_class random").c_str());
    }
}

void OnShutdown()
{
    if (*auto_queue)
        tfmm::StartQueue();
}

static CatCommand get_steamid("print_steamid", "Prints your SteamID", []() { g_ICvar->ConsoleColorPrintf(MENU_COLOR, "%u\n", g_ISteamUser->GetSteamID().GetAccountID()); });

static InitRoutine init(
    []()
    {
        EC::Register(EC::CreateMove, Update, "cm_autojoin", EC::average);
        EC::Register(EC::Paint, UpdateSearch, "paint_autojoin", EC::average);
        static auto p_sig = CSignature::GetClientSignature("55 89 E5 57 56 53 83 EC 1C 8B 5D ? 8B 75 ? 8B 7D ? 89 1C 24 89 74 24 ? 89 7C 24 ? E8 ? ? ? ? 84 C0") + 29;
        static BytePatch p{ e8call_direct(p_sig), { 0x31, 0xC0, 0x40, 0xC3 } };
        static BytePatch p2{ CSignature::GetClientSignature, "55 89 E5 57 56 53 83 EC ? 8B 45 0C 8B 5D 08 8B 55 10 89 45 ? 8B 43", 0x00, { 0x31, 0xC0, 0x40, 0xC3 } };
        if (*partybypass)
        {
            p.Patch();
            p2.Patch();
        }
        partybypass.installChangeCallback(
            [](settings::VariableBase<bool> &, bool new_val)
            {
                if (new_val)
                {
                    p.Patch();
                    p2.Patch();
                }
                else
                {
                    p.Shutdown();
                    p2.Shutdown();
                }
            });
        EC::Register(
            EC::Shutdown,
            []()
            {
                p.Shutdown();
                p2.Shutdown();
            },
            "shutdown_autojoin");
    });
} // namespace hacks::autojoin