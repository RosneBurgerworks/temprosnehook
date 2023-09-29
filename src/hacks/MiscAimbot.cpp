//
// Created by bencat07 on 28.09.18.
//

#include "common.hpp"
#include "settings/Bool.hpp"
#include "settings/Int.hpp"
#include "settings/Key.hpp"
#include "PlayerTools.hpp"
#include "MiscAimbot.hpp"
#include "DetourHook.hpp"

namespace hacks::misc_aimbot
{
constexpr float grav           = 0.25f;
constexpr float initial_vel    = 200.0f;
int prevent                    = -1;
static Timer previous_entity_delay{};

// TODO: Refactor this jank
std::pair<CachedEntity *, Vector> FindBestEnt(bool teammate, bool Predict, bool zcheck,  float range)
{
    CachedEntity *bestent                             = nullptr;
    float bestscr                                     = FLT_MAX;
    Vector predicted{};
    // Too long since we focused it
    if (previous_entity_delay.check(150))
        prevent = -1;

    for (int i = 0; i < 1; ++i)
    {
        if (prevent != -1)
        {
            auto ent = ENTITY(prevent);
            if (CE_BAD(ent) || !ent->m_bAlivePlayer() || (teammate && ent->m_iTeam() != LOCAL_E->m_iTeam()) || ent == LOCAL_E)
                continue;
            if (!teammate && ent->m_iTeam() == LOCAL_E->m_iTeam())
                continue;
            if (!ent->hitboxes.GetHitbox(1))
                continue;
            if (!teammate && !player_tools::shouldTarget(ent))
                continue;
            Vector target{};
            if (!IsEntityVectorVisible(ent, target))
                continue;
            if (zcheck && (ent->m_vecOrigin().z - LOCAL_E->m_vecOrigin().z) > 200.0f)
                continue;
            float scr                                    = ent->m_flDistance();
            if (g_pPlayerResource->GetClass(ent) == tf_medic)
                scr *= 0.5f;
            if (scr < bestscr)
            {
                bestent   = ent;
                predicted = target;
                bestscr   = scr;
                prevent   = ent->m_IDX;
            }
        }
        if (bestent && predicted.z)
        {
            previous_entity_delay.update();
            return { bestent, predicted };
        }
    }
    prevent = -1;
    for (auto const &ent : entity_cache::player_cache)
    {
        if (CE_BAD(ent) || !(ent->m_bAlivePlayer()) || (teammate && ent->m_iTeam() != LOCAL_E->m_iTeam()) || ent == LOCAL_E)
            continue;
        if (!teammate && ent->m_iTeam() == LOCAL_E->m_iTeam())
            continue;
        if (!ent->hitboxes.GetHitbox(1))
            continue;
        Vector target{};
        if (!IsEntityVectorVisible(ent, target))
            continue;
        if (zcheck && (ent->m_vecOrigin().z - LOCAL_E->m_vecOrigin().z) > 200.0f)
            continue;
        float scr                                    = ent->m_flDistance();
        if (g_pPlayerResource->GetClass(ent) == tf_medic)
            scr *= 0.5f;
        if (scr < bestscr)
        {
            bestent   = ent;
            predicted = target;
            bestscr   = scr;
            prevent   = ent->m_IDX;
        }
    }
    return { bestent, predicted };
}
static float slow_change_dist_y{};
static float slow_change_dist_p{};
void DoSlowAim(Vector &input_angle, float speed)
{
    auto viewangles = current_user_cmd->viewangles;

    // Don't bother if we're already on target (unlikely)
    if (viewangles != input_angle) [[likely]]
    {
        Vector slow_delta = input_angle - viewangles;

        slow_delta.y = std::fmod(slow_delta.y + 180.0f, 360.0f) - 180.0f;

        slow_delta /= speed;
        input_angle = viewangles + slow_delta;

        // Clamp as we changed angles
        fClampAngle(input_angle);
    }
}

static void CreateMove()
{

}

DetourHook CAM_CapYaw_detour;
typedef float (*CAM_CapYaw_t)(IInput *, float);
// Fix client side limit being applied weirdly, Note that most of this is taken from the source leak directly
float CAM_CapYaw_Hook(IInput *this_, float fVal)
{
    if (CE_INVALID(LOCAL_E))
        return fVal;

    if (HasCondition<TFCond_Charging>(LOCAL_E))
    {
        float flChargeYawCap = re::CTFPlayerShared::CalculateChargeCap(re::CTFPlayerShared::GetPlayerShared(RAW_ENT(LOCAL_E)));

        // Our only change
        flChargeYawCap *= 2.5f;

        if (fVal > flChargeYawCap)
            return flChargeYawCap;
        else if (fVal < -flChargeYawCap)
            return -flChargeYawCap;
    }

    return fVal;
}

#define foffset(p, i) ((uint8_t *) &p)[i]
static InitRoutine init(
    []()
    {
        EC::Register(EC::CreateMove, CreateMove, "cm_miscaimbot", EC::average);
        EC::Register(EC::CreateMoveWarp, CreateMove, "cmw_miscaimbot", EC::average);

        static auto signature = CSignature::GetClientSignature("55 89 E5 53 83 EC 14 E8 ? ? ? ? 85 C0 74 ? 8D 98 ? ? ? ? C7 44 24 ? 11 00 00 00");

        CAM_CapYaw_detour.Init(signature, (void *) CAM_CapYaw_Hook);
        EC::Register(
            EC::Shutdown, []() { CAM_CapYaw_detour.Shutdown(); }, "chargeaim_shutdown");
    });
} // namespace hacks::misc_aimbot
