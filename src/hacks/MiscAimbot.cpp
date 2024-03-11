//
// Created by bencat07 on 28.09.18.
//

#include "common.hpp"
#include "settings/Bool.hpp"
#include "PlayerTools.hpp"
#include "MiscAimbot.hpp"
#include "DetourHook.hpp"
#include "Backtrack.hpp"

namespace hacks::misc_aimbot
{
constexpr float grav           = 0.25f;
constexpr float initial_vel    = 200.0f;
int prevent                    = -1;
static Timer previous_entity_delay{};

std::pair<CachedEntity *, Vector> FindBestEnt(bool teammate, bool Predict, bool zcheck, float range)
{
    CachedEntity *bestent                             = nullptr;
    float bestscr                                     = FLT_MAX;
    std::optional<backtrack::BacktrackData> best_data = std::nullopt;
    Vector predicted{};
    // Too long since we focused it
    if (previous_entity_delay.check(100)) 
        prevent = -1;

    bool shouldBacktrack = backtrack::backtrackEnabled() && !backtrack::hasData();

    auto calculateEntity = [&](CachedEntity *ent) -> std::optional<std::tuple<float, Vector, std::optional<backtrack::BacktrackData>>>
    {
        if (RAW_ENT(ent)->IsDormant() || (teammate && ent->m_iTeam() != g_pLocalPlayer->team) || ent == LOCAL_E)
            return std::nullopt;
        if (!teammate && ent->m_iTeam() == g_pLocalPlayer->team)
            return std::nullopt;
        if (!ent->hitboxes.GetHitbox(1))
            return std::nullopt;
        if (!teammate && !player_tools::shouldTarget(ent))
            return std::nullopt;

        Vector target{};
        target = ent->hitboxes.GetHitbox(1)->center;

        if (!shouldBacktrack && !IsEntityVectorVisible(ent, target))
            return std::nullopt;

        if (zcheck && (ent->m_vecOrigin().z - g_pLocalPlayer->v_Origin.z) > 200.0f)
            return std::nullopt;

        float scr = ent->m_flDistance();

        std::optional<backtrack::BacktrackData> data = std::nullopt;

        if (g_pPlayerResource->GetClass(ent) == tf_medic)
            scr *= 0.5f;

        return { { scr, target, data } };
    };

    for (const auto &ent : entity_cache::player_cache)
    {
        if (prevent != -1)
        {
            auto result = calculateEntity(ent);
            if (result && std::get<0>(*result) < bestscr)
            {
                bestent   = ent;
                predicted = std::get<1>(*result);
                bestscr   = std::get<0>(*result);
                prevent   = ent->m_IDX;
                if (shouldBacktrack)
                    best_data = std::get<2>(*result);
            }
            previous_entity_delay.update();
        }
    }
    return { bestent, predicted };
}

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

// auto repair
static settings::Int autorepair_priority{ "autorepair.priority", "0" };
static settings::Boolean autorepair_enabled("autorepair.enabled", "false");
static settings::Boolean autorepair_silent("autorepair.silent", "true");
static settings::Boolean autorepair_repair_sentry("autorepair.sentry", "false");
static settings::Boolean autorepair_repair_dispenser("autorepair.dispenser", "false");
static settings::Boolean autorepair_repair_teleport("autorepair.teleport", "false");

// auto upgrade
static settings::Boolean autoupgrade_enabled("autoupgrade.enabled", "false");
static settings::Boolean autoupgrade_sentry("autoupgrade.sentry", "false");
static settings::Boolean autoupgrade_dispenser("autoupgrade.dispenser", "false");
static settings::Boolean autoupgrade_teleport("autoupgrade.teleport", "false");
static settings::Int autoupgrade_sentry_level("autoupgrade.sentry.level", "3");
static settings::Int autoupgrade_dispenser_level("autoupgrade.dispenser.level", "3");
static settings::Int autoupgrade_teleport_level("autoupgrade.teleport.level", "2");

bool ShouldHitBuilding(CachedEntity *ent)
{
    if (!autoupgrade_enabled && !autorepair_enabled)
        return false;
    // Current Metal
    int cur_ammo = CE_INT(LOCAL_E, netvar.m_iAmmo + 12);
    // Autorepair is on
    if (autorepair_enabled)
    {
        // Special Sentry logic
        if (ent->m_iClassID() == CL_CLASS(CObjectSentrygun))
        {
            // Current sentry ammo
            int sentry_ammo = CE_INT(ent, netvar.m_iAmmoShells);
            // Max Sentry ammo
            int max_ammo = 0;

            // Set Ammo depending on level
            switch (CE_INT(ent, netvar.iUpgradeLevel))
            {
            case 1:
                max_ammo = 150;
                break;
            case 2:
            case 3:
                max_ammo = 200;
            }

            // Sentry needs ammo
            if (sentry_ammo < max_ammo)
                return true;
        }
        // Buildings needs to be repaired
        if (cur_ammo && ent->m_iHealth() != ent->m_iMaxHealth())
            return true;
    }
    // Autoupgrade is on
    if (autoupgrade_enabled)
    {
        // Upgrade lvel
        int upgrade_level = CE_INT(ent, netvar.iUpgradeLevel);

        // Don't upgrade mini sentries
        if (CE_BYTE(ent, netvar.m_bMiniBuilding))
            return false;

        // Rvar to check
        int level = 0;

        // Pick The right rvar to check depending on building type
        switch (ent->m_iClassID())
        {

        case CL_CLASS(CObjectSentrygun):
            // Enabled check
            if (!autoupgrade_sentry)
                return false;
            level = *autoupgrade_sentry_level;
            break;

        case CL_CLASS(CObjectDispenser):
            // Enabled check
            if (!autoupgrade_dispenser)
                return false;
            level = *autoupgrade_dispenser_level;
            break;

        case CL_CLASS(CObjectTeleporter):
            // Enabled check
            if (!autoupgrade_teleport)
                return false;
            level = *autoupgrade_teleport_level;
            break;
        }

        // Can be upgraded
        if (upgrade_level < level && cur_ammo)
            return true;
    }
    return false;
}

CachedEntity *targetBuilding(bool priority)
{
    float wrench_range   = re::C_TFWeaponBaseMelee::GetSwingRange(RAW_ENT(LOCAL_W));
    CachedEntity *target = nullptr;
    float distance       = FLT_MAX;
    for (const auto &ent : entity_cache::valid_ents)
    {
        // can't exactly repair Merasmus
        if (ent->m_Type() != ENTITY_BUILDING)
            continue;

        // yes, repair enemy buildings
        if (ent->m_bEnemy())
            continue;

        float new_distance = g_pLocalPlayer->v_Eye.DistTo(GetBuildingPosition(ent));

        // Further away than old one
        if (new_distance >= distance)
            continue;

        auto id = ent->m_iClassID();

        switch (id)
        {

        case CL_CLASS(CObjectSentrygun):
        {
            if (priority && *autorepair_priority != 1)
                continue;
            // Don't repair sentries
            if (!autorepair_repair_sentry && !autoupgrade_sentry)
                continue;
            break;
        }

        case CL_CLASS(CObjectDispenser):
        {
            if (priority && *autorepair_priority != 2)
                continue;
            // Repair Dispensers check
            if (!autorepair_repair_dispenser && !autoupgrade_dispenser)
                continue;
            break;
        }

        case CL_CLASS(CObjectTeleporter):
        {
            if (priority && *autorepair_priority != 3)
                continue;
            // Repair Teleporters check
            if (!autorepair_repair_teleport && !autoupgrade_teleport)
                continue;
            break;
        }

        default:
            continue;
        }

        float s_distance = ent->m_vecOrigin().DistTo(LOCAL_E->m_vecOrigin());

        if (!ShouldHitBuilding(ent))
            continue;

        if (s_distance > wrench_range)
            continue;

        target = ent;

        distance = new_distance;
    }
    return target;
}

static void BuildingAimbot()
{
    if (!autorepair_enabled && !autoupgrade_enabled)
        return;

    if (CE_BAD(LOCAL_E) || CE_BAD(LOCAL_W))
        return;

    CachedEntity *target = nullptr;
    // Metal
    int cur_ammo       = CE_INT(LOCAL_E, netvar.m_iAmmo + 12);
    float wrench_range = re::C_TFWeaponBaseMelee::GetSwingRange(RAW_ENT(LOCAL_W));
    // Center is further away than actual hit range for buildings, so add some more
    wrench_range += 50.f;

    if (cur_ammo == 0)
        return;

    if (g_pLocalPlayer->clazz != tf_engineer)
        return;

    if (GetWeaponMode() != weaponmode::weapon_melee)
        return;

    // Get priority buildings
    if (autorepair_priority)
        target = targetBuilding(true);
    // No building found
    if (!target)
        targetBuilding(false);

    // We have a target
    if (target)
    {
        Vector angle   = GetAimAtAngles(g_pLocalPlayer->v_Eye, GetBuildingPosition(target));
        Vector forward = GetForwardVector(g_pLocalPlayer->v_Eye, angle, wrench_range, LOCAL_E);

        trace_t trace;

        if (IsEntityVectorVisible(target, forward, false, MASK_SHOT, &trace))
        {
            if (trace.DidHit() && (IClientEntity *) trace.m_pEnt == RAW_ENT(target))
            {
                current_user_cmd->viewangles = angle;
                if (autorepair_silent)
                    g_pLocalPlayer->bUseSilentAngles = true;
                current_user_cmd->buttons |= IN_ATTACK;
            }
        }
    }
}

static void CreateMove()
{
    BuildingAimbot();
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