/*
 * Aimbot.cpp
 *
 *  Created on: Oct 9, 2016
 *      Author: nullifiedcat
 */

#include <hacks/Aimbot.hpp>
#include <hacks/AntiAim.hpp>
#include <hacks/ESP.hpp>
#include <hacks/Backtrack.hpp>
#include <PlayerTools.hpp>
#include <settings/Bool.hpp>
#include "common.hpp"
#include <targethelper.hpp>
#include "MiscTemporary.hpp"
#include "hitrate.hpp"
#include "Warp.hpp"
#include "NavBot.hpp"

namespace hacks::aimbot
{
static settings::Boolean normal_enable{ "aimbot.enable", "true" };
static settings::Button aimkey{ "aimbot.aimkey.button", "<null>" };
static settings::Int aimkey_mode{ "aimbot.aimkey.mode", "1" };
static settings::Boolean autoshoot{ "aimbot.autoshoot", "true" };
static settings::Boolean autoreload{ "aimbot.autoshoot.activate-heatmaker", "true" };
static settings::Boolean multipoint{ "aimbot.multipoint", "0" };
static settings::Int vischeck_hitboxes{ "aimbot.vischeck-hitboxes", "0" };
static settings::Int hitbox_mode{ "aimbot.hitbox-mode", "2" }; // This makes aimbot fucking hit p
static settings::Float normal_fov{ "aimbot.fov", "0" };
static settings::Int priority_mode{ "aimbot.priority-mode", "0" };
static settings::Boolean wait_for_charge{ "aimbot.wait-for-charge", "false" };

static settings::Boolean silent{ "aimbot.silent", "true" };
static settings::Boolean target_lock{ "aimbot.lock-target", "true" };
#if ENABLE_VISUALS
static settings::Boolean fov_draw{ "aimbot.fov-circle.enable", "0" };
static settings::Float fovcircle_opacity{ "aimbot.fov-circle.opacity", "0.7" };
#endif
static settings::Int hitbox{ "aimbot.hitbox", "0" };
static settings::Boolean zoomed_only{ "aimbot.zoomed-only", "true" };
static settings::Boolean only_can_shoot{ "aimbot.can-shoot-only", "true" };

static settings::Int normal_slow_aim{ "aimbot.slow", "0" };

static settings::Boolean auto_spin_up{ "aimbot.auto.spin-up", "false" };
static settings::Boolean minigun_tapfire{ "aimbot.auto.tapfire", "false" };
static settings::Boolean auto_zoom{ "aimbot.auto.zoom", "true" };
static settings::Boolean auto_unzoom{ "aimbot.auto.unzoom", "true" };
static settings::Float zoom_distance{ "aimbot.zoom.distance", "2000.0" };
static settings::Float rev_distance{ "aimbot.rev.distance", "1500.0" };

static settings::Boolean backtrack_aimbot{ "aimbot.backtrack", "false" };
static settings::Boolean backtrack_last_tick_only("aimbot.backtrack.only-last-tick", "true");
static bool force_backtrack_aimbot = false;

static settings::Boolean target_hazards{ "aimbot.target.hazards", "true" }; 
static settings::Float max_range{ "aimbot.target.max-range", "4096" };
static settings::Boolean ignore_vaccinator{ "aimbot.target.ignore-vaccinator", "false" };
static settings::Boolean buildings_sentry{ "aimbot.target.sentry", "true" };
static settings::Boolean npcs{ "aimbot.target.npcs", "true" };
static settings::Int teammates{ "aimbot.target.teammates", "0" };

settings::Boolean engine_projpred{ "aimbot.debug.engine-pp", "true" };

struct AimbotCalculatedData_s
{
    unsigned long predict_tick{ 0 };
    bool predict_type{ false };
    Vector aim_position{ 0 };
    unsigned long vcheck_tick{ 0 };
    bool visible{ false };
    float fov{ 0 };
    int hitbox{ 0 };
} static cd;

int slow_aim;
float fov;
bool enable;

int previous_x, previous_y;
int current_x, current_y;

float last_mouse_check = 0;
float stop_moving_time = 0;

// Used to make rapidfire not knock your enemies out of range
unsigned int last_target_ignore_timer = 0;
bool shouldbacktrack_cache = false;

// Func to find value of how far to target ents
inline float EffectiveTargetingRange()
{
    if (GetWeaponMode() == weapon_melee)
        return static_cast<float>(re::C_TFWeaponBaseMelee::GetSwingRange(RAW_ENT(LOCAL_W)));
    switch (LOCAL_W->m_iClassID())
    {
    case CL_CLASS(CTFFlameThrower):
        return 310.0f; // Pyros only have so much until their flames hit
    case CL_CLASS(CTFWeaponFlameBall):
        return 512.0f; // Dragons Fury is fast but short range
    default:
        return *max_range;
    }
}

inline bool IsHitboxMedium(int hitbox)
{
    return hitbox >= 1 && hitbox <= 5;
}

inline bool PlayerTeamCheck(CachedEntity *entity)
{
    return *teammates == 2 || !*teammates && entity->m_bEnemy() || *teammates && !entity->m_bEnemy() || CE_GOOD(LOCAL_W) && LOCAL_W->m_iClassID() == CL_CLASS(CTFCrossbow) && entity->m_iHealth() < entity->m_iMaxHealth();
}

// Am I holding the Hitman's Heatmaker ?
inline bool CarryingHeatmaker()
{
    return CE_INT(LOCAL_W, netvar.iItemDefinitionIndex) == 752;
}

// Am I holding the Machina ?
inline bool CarryingMachina()
{
    int item_definition_index = CE_INT(LOCAL_W, netvar.iItemDefinitionIndex);
    return item_definition_index == 526 || item_definition_index == 30665;
}

// A function to find the best hitbox for a target
inline int BestHitbox(CachedEntity *target)
{
    // Switch based upon the hitbox mode set by the user
    switch (*hitbox_mode)
    {
    case 0: // AUTO priority
        return AutoHitbox(target);
    case 1: // AUTO priority, return closest hitbox to crosshair
        return ClosestHitbox(target);
    case 2: // STATIC priority, return a user chosen hitbox
        return *hitbox;
    }
    // Hitbox machine :b:roke
    return -1;
}

inline void UpdateShouldBacktrack()
{
    if (hacks::backtrack::hasData() || !(*backtrack_aimbot || force_backtrack_aimbot))
        shouldbacktrack_cache = false;
    else
        shouldbacktrack_cache = true;
}

inline bool ShouldBacktrack(CachedEntity *ent)
{
    if (!shouldbacktrack_cache || ent && ent->m_Type() != ENTITY_PLAYER || !backtrack::getGoodTicks(ent))
        return false;
    return true;
}

#define GET_MIDDLE(c1, c2) ((corners[c1] + corners[c2]) / 2.0f)

// Get all the valid aim positions
std::vector<Vector> GetValidHitpoints(CachedEntity *ent, int hitbox)
{
    // Recorded vischeckable points
    std::vector<Vector> hitpoints;
    auto hb = ent->hitboxes.GetHitbox(hitbox);

    trace_t trace;
    if (IsEntityVectorVisible(ent, hb->center, true, MASK_SHOT_HULL, &trace, true) && trace.hitbox == hitbox)
        hitpoints.push_back(hb->center);

    // Multipoint
    auto bboxmin = hb->bbox->bbmin;
    auto bboxmax = hb->bbox->bbmax;

    auto transform = ent->hitboxes.GetBones()[hb->bbox->bone];
    QAngle rotation;
    Vector origin;

    MatrixAngles(transform, rotation, origin);

    Vector corners[8];
    GenerateBoxVertices(origin, rotation, bboxmin, bboxmax, corners);

    float shrink_size = 1;

    if (!IsHitboxMedium(hitbox)) // hitbox should be chosen based on size.
        shrink_size = 3;
    else
        shrink_size = 6;

    // Shrink positions by moving towards opposing corner
    for (uint8 i = 0; i < 8; ++i)
        corners[i] += (corners[7 - i] - corners[i]) / shrink_size;

    // Generate middle points on line segments
    // Define cleans up code

    const Vector line_positions[12] = { GET_MIDDLE(0, 1), GET_MIDDLE(0, 2), GET_MIDDLE(1, 3), GET_MIDDLE(2, 3), GET_MIDDLE(7, 6), GET_MIDDLE(7, 5), GET_MIDDLE(6, 4), GET_MIDDLE(5, 4), GET_MIDDLE(0, 4), GET_MIDDLE(1, 5), GET_MIDDLE(2, 6), GET_MIDDLE(3, 7) };

    // Create combined vector
    std::vector<Vector> positions;

    positions.reserve(sizeof(Vector) * 20);
    positions.insert(positions.end(), corners, &corners[8]);
    positions.insert(positions.end(), line_positions, &line_positions[12]);

    for (uint8 i = 0; i < 20; ++i)
    {
        trace_t trace;
        if (IsEntityVectorVisible(ent, positions[i], true, MASK_SHOT_HULL, &trace, true) && trace.hitbox == hitbox)
            hitpoints.push_back(positions[i]);
    }

    if (*vischeck_hitboxes)
    {
        if (*vischeck_hitboxes == 1 && playerlist::AccessData(ent->player_info->friendsID).state != playerlist::k_EState::RAGE)
            return hitpoints;

        int i                  = 0;
        const u_int8_t max_box = ent->hitboxes.GetNumHitboxes();
        while (hitpoints.empty() && i < max_box) // Prevents returning empty at all costs. Loops through every hitbox
        {
            if (hitbox == i)
            {
                ++i;
                continue;
            }
            hitpoints = GetHitpointsVischeck(ent, i);
            ++i;
        }
    }

    return hitpoints;
}

std::vector<Vector> GetHitpointsVischeck(CachedEntity *ent, int hitbox)
{
    std::vector<Vector> hitpoints;
    auto hb      = ent->hitboxes.GetHitbox(hitbox);
    auto bboxmin = hb->bbox->bbmin;
    auto bboxmax = hb->bbox->bbmax;

    auto transform = ent->hitboxes.GetBones()[hb->bbox->bone];
    QAngle rotation;
    Vector origin;

    MatrixAngles(transform, rotation, origin);

    Vector corners[8];
    GenerateBoxVertices(origin, rotation, bboxmin, bboxmax, corners);

    float shrink_size = 1;

    if (!IsHitboxMedium(hitbox)) // hitbox should be chosen based on size.
        shrink_size = 3;
    else
        shrink_size = 6;

    // Shrink positions by moving towards opposing corner
    for (uint8 i = 0; i < 8; ++i)
        corners[i] += (corners[7 - i] - corners[i]) / shrink_size;

    // Generate middle points on line segments
    // Define cleans up code
    const Vector line_positions[12] = { GET_MIDDLE(0, 1), GET_MIDDLE(0, 2), GET_MIDDLE(1, 3), GET_MIDDLE(2, 3), GET_MIDDLE(7, 6), GET_MIDDLE(7, 5), GET_MIDDLE(6, 4), GET_MIDDLE(5, 4), GET_MIDDLE(0, 4), GET_MIDDLE(1, 5), GET_MIDDLE(2, 6), GET_MIDDLE(3, 7) };

    // Create combined vector
    std::vector<Vector> positions;

    positions.reserve(sizeof(Vector) * 20);
    positions.insert(positions.end(), corners, &corners[8]);
    positions.insert(positions.end(), line_positions, &line_positions[12]);

    for (uint8 i = 0; i < 20; ++i)
    {
        trace_t trace;
        if (IsEntityVectorVisible(ent, positions[i], true, MASK_SHOT_HULL, &trace, true) && trace.hitbox == hitbox)
            hitpoints.push_back(positions[i]);
    }

    return hitpoints;
}

// Get the best point to aim at for a given hitbox
std::optional<Vector> GetBestHitpoint(CachedEntity *ent, int hitbox)
{
    auto positions = GetValidHitpoints(ent, hitbox);

    std::optional<Vector> best_pos = std::nullopt;
    float max_score                = FLT_MAX;
    for (const auto &position : positions)
    {
        float score = GetFov(g_pLocalPlayer->v_OrigViewangles, g_pLocalPlayer->v_Eye, position);
        if (score < max_score)
        {
            best_pos  = position;
            max_score = score;
        }
    }
    return best_pos;
}

// Reduce Backtrack lag by checking if the ticks hitboxes are within a reasonable FOV range
bool ValidateTickFov(backtrack::BacktrackData &tick)
{
    if (fov)
    {
        bool valid_fov = false;
        for (const auto &hitbox : tick.hitboxes)
        {
            float score = GetFov(g_pLocalPlayer->v_OrigViewangles, g_pLocalPlayer->v_Eye, hitbox.center);
            // Check if the FOV is within a 2.0f threshhold
            if (score < fov + 2.0f)
            {
                valid_fov = true;
                break;
            }
        }
        return valid_fov;
    }
    return true;
}

bool AllowNoScope(CachedEntity *target)
{
    if (!target || CarryingMachina())
        return false;

    int target_health       = target->m_iHealth();
    bool carrying_heatmaker = CarryingHeatmaker();

    if (!carrying_heatmaker && target_health <= 50)
        return true;

    if (carrying_heatmaker && target_health <= 40)
        return true;

    bool local_player_crit_boosted = IsPlayerCritBoosted(LOCAL_E);
    if (local_player_crit_boosted && target_health <= 150)
        return true;

    bool local_player_mini_crit_boosted = IsPlayerMiniCritBoosted(LOCAL_E);
    if (local_player_mini_crit_boosted && target_health <= 68 && !carrying_heatmaker)
        return true;

    if (local_player_mini_crit_boosted && target_health <= 54 && carrying_heatmaker)
        return true;

    return false;
}

void DoAutoZoom(bool target_found, CachedEntity *target)
{
    // Keep track of our zoom time
    static Timer zoom_time{};
    auto nearest = hacks::NavBot::getNearestPlayerDistance();

    // rev distance
    if (LOCAL_W->m_iClassID() == CL_CLASS(CTFMinigun) && (target_found || nearest.second <= *rev_distance))
    {
        if (target_found)
            zoom_time.update();
        if (!g_pLocalPlayer->bRevved || !g_pLocalPlayer->bRevving)
            current_user_cmd->buttons |= IN_ATTACK2;
    }

    // Minigun spun up handler
    if (*auto_spin_up && LOCAL_W->m_iClassID() == CL_CLASS(CTFMinigun) && !*rev_distance)
    {
        if (target_found)
            zoom_time.update();
        if (!zoom_time.check(3000))
            current_user_cmd->buttons |= IN_ATTACK2;
        return;
    }

    if (g_pLocalPlayer->holding_sniper_rifle && !AllowNoScope(target) && (target_found || nearest.second <= *zoom_distance))
    {
        if (target_found)
            zoom_time.update();
        if (!g_pLocalPlayer->bZoomed)
            current_user_cmd->buttons |= IN_ATTACK2;
    }
    // Auto-Unzoom
    else if (*auto_unzoom && zoom_time.check(3000) && !target_found && g_pLocalPlayer->holding_sniper_rifle && g_pLocalPlayer->bZoomed && nearest.second > *zoom_distance)
        current_user_cmd->buttons |= IN_ATTACK2;
}

// Current Entity
CachedEntity *target_last = nullptr;
bool aimed_this_tick      = false;
Vector viewangles_this_tick(0.0f);

// If slow aimbot allows autoshoot
bool slow_can_shoot = false;

// The main "loop" of the aimbot.
static void CreateMove()
{
    enable          = *normal_enable;
    slow_aim        = *normal_slow_aim;
    fov             = *normal_fov;
    aimed_this_tick = false;

    bool aimkey_status = UpdateAimkey();

    if (!enable || !LOCAL_E || !LOCAL_E->m_bAlivePlayer() || !aimkey_status || !ShouldAim())
    {
        target_last = nullptr;
        return;
    }

    if (*only_can_shoot && !slow_aim && !CanShoot())
        return;

    DoAutoZoom(false, nullptr);

    bool should_backtrack    = hacks::backtrack::backtrackEnabled();
    int weapon_mode          = GetWeaponMode();
    bool should_zoom         = *auto_zoom;
    switch (weapon_mode)
    {
    case weapon_hitscan:
        if (should_backtrack)
            UpdateShouldBacktrack();
        target_last = RetrieveBestTarget(aimkey_status);
        if (target_last)
        {
            if (should_zoom)
                DoAutoZoom(true, target_last);
            int weapon_case = LOCAL_W->m_iClassID();
            if (!HitscanSpecialCases(target_last, weapon_case))
                DoAutoshoot(target_last);
        }
        break;
    case weapon_melee:
        if (should_backtrack)
            UpdateShouldBacktrack();
        target_last = RetrieveBestTarget(aimkey_status);
        if (target_last)
            DoAutoshoot(target_last);
        break;
        [[fallthrough]];
    default:
        break;
    }
}

int tapfire_delay = 0;
bool HitscanSpecialCases(CachedEntity *target_entity, int weapon_case)
{
    if (weapon_case == CL_CLASS(CTFMinigun))
    {
        if (!*minigun_tapfire)
            DoAutoshoot(target_entity);
        else
        {
            // Used to keep track of what tick we're in right now
            tapfire_delay++;

            // This is the exact delay needed to hit
            if (tapfire_delay >= 17 || target_entity->m_flDistance() <= 1250.0f)
            {
                DoAutoshoot(target_entity);
                tapfire_delay = 0;
            }
            return true;
        }
        return true;
    }
    else
        return false;
}

// Just hold m1 if we were aiming at something before and are in rapidfire
static void CreateMoveWarp()
{
    if (hacks::warp::in_rapidfire && aimed_this_tick)
    {
        current_user_cmd->viewangles     = viewangles_this_tick;
        g_pLocalPlayer->bUseSilentAngles = *silent;
        current_user_cmd->buttons |= IN_ATTACK;
    }
    // Warp should call aimbot normally
    else if (!hacks::warp::in_rapidfire)
        CreateMove();
}

#if ENABLE_VISUALS
bool MouseMoving()
{
    if (SERVER_TIME - last_mouse_check < 0.02)
        SDL_GetMouseState(&previous_x, &previous_y);
    else
    {
        SDL_GetMouseState(&current_x, &current_y);
        last_mouse_check = SERVER_TIME;
    }

    if (previous_x != current_x || previous_y != current_y)
        stop_moving_time = SERVER_TIME + 0.5;

    return SERVER_TIME <= stop_moving_time;
}
#endif

// The first check to see if the player should aim in the first place
bool ShouldAim()
{
    // Checks should be in order: cheap -> expensive
    // Check for +use
    if (current_user_cmd->buttons & IN_USE)
        return false;
    // Check if using action slot item
    if (g_pLocalPlayer->using_action_slot_item)
        return false;
    // Our team lost, so we can't hurt the enemy team
    if (TFGameRules()->RoundHasBeenWon() && TFGameRules()->GetWinningTeam() != g_pLocalPlayer->team)
        return false;
    // Carrying A building?
    if (CE_BYTE(LOCAL_E, netvar.m_bCarryingObject))
        return false;
    // Is bonked?
    if (HasCondition<TFCond_Bonked>(LOCAL_E))
        return false;
    // Is taunting?
    if (HasCondition<TFCond_Taunting>(LOCAL_E))
        return false;
    // Is cloaked?
    if (IsPlayerInvisible(LOCAL_E))
        return false;
    // Using the minigun and we have no ammo?
    if (LOCAL_W->m_iClassID() == CL_CLASS(CTFMinigun) && CE_INT(LOCAL_E, netvar.m_iAmmo + 4) == 0)
        return false;
    return true;
}

// Function to find a suitable target
CachedEntity *RetrieveBestTarget(bool aimkey_state)
{
    // If target lock is on, we've already chosen a target, and the aimkey is allowed, then attempt to keep the previous target
    if (*target_lock && target_last && aimkey_state && ShouldBacktrack(target_last))
    {
        auto good_ticks_tmp = hacks::backtrack::getGoodTicks(target_last);
        if (good_ticks_tmp)
        {
            auto good_ticks = *good_ticks_tmp;
            if (*backtrack_last_tick_only)
            {
                good_ticks.clear();
                good_ticks.push_back(good_ticks_tmp->back());
            }

            for (auto &bt_tick : good_ticks)
            {
                if (!ValidateTickFov(bt_tick))
                    continue;
                hacks::backtrack::MoveToTick(bt_tick);
                if (IsTargetStateGood(target_last) && Aim(target_last))
                    return target_last;
                // Restore if bad target
                hacks::backtrack::RestoreEntity(target_last->m_IDX);
            }
        }
        // Check if previous target is still good
        else if (!shouldbacktrack_cache && IsTargetStateGood(target_last) && Aim(target_last))
            return target_last;
    }
    // No last_target found, reset the timer.
    last_target_ignore_timer = 0;

    // Do not attempt to target hazards using melee weapons
    // Competitive maps do not have any hazards
    if (*target_hazards && GetWeaponMode() != weapon_melee && !TFGameRules()->m_bCompetitiveMode)
    {
        for (const auto &pEntity : entity_cache::valid_ents)
        {
            const model_t *pModel = RAW_ENT(pEntity)->GetModel();
            const char *pszName = g_IModelInfo->GetModelName(pModel);
            if (Hash::IsHazard(pszName))
            {
                // TODO: Test if hazards hurt NPCs
                for (const auto &pPlayer : entity_cache::player_cache)
                {
                    const Vector vecHazardOrigin = pEntity->m_vecOrigin();
                    const Vector vecEntityOrigin = pPlayer->m_vecOrigin();
                    if (IsTargetStateGood(pPlayer) && IsVectorVisible(vecHazardOrigin, vecEntityOrigin, true))
                    {
                        const float flDistHazardToTarget       = vecHazardOrigin.DistTo(vecEntityOrigin);
                        const float flDistHazardToLocalPlayer  = pEntity->m_flDistance();
                        const float flDamageToTarget           = 150.0f - 0.25f * flDistHazardToTarget;
                        // Hazards cannot deal less than 75 damage
                        if (flDamageToTarget >= 75.0f && flDistHazardToLocalPlayer > 350.0f && Aim(pEntity))
                        {
                            return pEntity;
                        }
                    }
                }
            }
        }
    }

    float target_highest_score, score = 0.0f;
    CachedEntity *target_highest_ent                       = nullptr;
    target_highest_score                                   = -256.0f;
    std::optional<hacks::backtrack::BacktrackData> bt_tick = std::nullopt;
    for (const auto &ent : entity_cache::valid_ents)
    {
        if (RAW_ENT(ent)->IsDormant())
        {
            continue;
        }

        // Check whether the current ent is good enough to target
        bool good_target = false;

        static std::optional<hacks::backtrack::BacktrackData> temp_bt_tick = std::nullopt;
        if (ShouldBacktrack(ent))
        {
            auto good_ticks_tmp = backtrack::getGoodTicks(ent);
            if (good_ticks_tmp)
            {
                auto good_ticks = *good_ticks_tmp;
                if (*backtrack_last_tick_only)
                {
                    good_ticks.clear();
                    good_ticks.push_back(good_ticks_tmp->back());
                }
                for (auto &bt_tick : good_ticks)
                {
                    if (!ValidateTickFov(bt_tick))
                        continue;
                    hacks::backtrack::MoveToTick(bt_tick);
                    if (IsTargetStateGood(ent) && Aim(ent))
                    {
                        good_target  = true;
                        temp_bt_tick = bt_tick;
                        break;
                    }
                    hacks::backtrack::RestoreEntity(ent->m_IDX);
                }
            }
        }
        else if (IsTargetStateGood(ent) && Aim(ent))
            good_target = true;

        if (good_target) // Melee mode straight up won't swing if the target is too far away. No need to prioritize based on distance. Just use whatever the user chooses.
        {
            switch (*priority_mode)
            {
            case 0: // Smart Priority
                score = static_cast<float>(GetScoreForEntity(ent));
                break;
            case 1: // Fov Priority
                score = 360.0f - cd.fov;
                break;
            case 2: // Distance Priority (Closest)
                score = 4096.0f - cd.aim_position.DistTo(g_pLocalPlayer->v_Eye);
                break;
            case 3: // Health Priority (Lowest)
                score = 450.0f - static_cast<float>(ent->m_iHealth());
                break;
            case 4: // Distance Priority (Furthest Away)
                score = cd.aim_position.DistTo(g_pLocalPlayer->v_Eye);
                break;
            case 5: // Health Priority (Highest)
                score = static_cast<float>(ent->m_iHealth()) * 4.0f;
                break;
            case 6: // Fast
            default:
                return ent;
            }
            // Crossbow logic
            if (!ent->m_bEnemy() && ent->m_Type() == ENTITY_PLAYER && CE_GOOD(LOCAL_W) && LOCAL_W->m_iClassID() == CL_CLASS(CTFCrossbow))
                score = static_cast<float>(ent->m_iMaxHealth() - ent->m_iHealth()) / static_cast<float>(ent->m_iMaxHealth()) * (*priority_mode == 2 ? 16384.0f : 2000.0f);

            // Compare the top score to our current ents score
            if (score > target_highest_score)
            {
                target_highest_score = score;
                target_highest_ent   = ent;
                bt_tick              = temp_bt_tick;
            }
        }

        // Restore tick
        if (ShouldBacktrack(ent))
            hacks::backtrack::RestoreEntity(ent->m_IDX);
    }

    if (target_highest_ent && bt_tick)
        hacks::backtrack::MoveToTick(*bt_tick);
    return target_highest_ent;
}

// A second check to determine whether a target is good enough to be aimed at
bool IsTargetStateGood(CachedEntity *entity)
{

    const int current_type = entity->m_Type();
    switch (current_type)
    {
    case (ENTITY_PLAYER):
    {
        // Local player check
        if (entity == LOCAL_E)
            return false;
        // Dead
        if (!entity->m_bAlivePlayer())
            return false;
        // Teammates
        if (!PlayerTeamCheck(entity))
            return false;
        if (!player_tools::shouldTarget(entity))
            return false;
        // Invulnerable players, ex: uber, bonk
        if (IsPlayerInvulnerable(entity))
            return false;
        // Distance
        float targeting_range = EffectiveTargetingRange();
        if (entity->m_flDistance() - 40.0f > targeting_range && tickcount > last_target_ignore_timer) // m_flDistance includes the collision box. You have to subtract it (Should be the same for every model)
            return false;

        // Wait for charge
        if (*wait_for_charge && g_pLocalPlayer->holding_sniper_rifle)
        {
            float cdmg  = CE_FLOAT(LOCAL_W, netvar.flChargedDamage) * 3.0f;
            float maxhs = 450.0f;
            if (CE_INT(LOCAL_W, netvar.iItemDefinitionIndex) == 230 || HasCondition<TFCond_Jarated>(entity))
            {
                modf(CE_FLOAT(LOCAL_W, netvar.flChargedDamage) * 1.35f, &cdmg);
                maxhs = 203.0f;
            }
            bool maxCharge = cdmg >= maxhs;

            // Vaccinator damage correction, Vac charge protects against 75% of damage
            if (IsPlayerInvisible(entity))
                cdmg = cdmg * 0.80f - 1.0f;
            else if (HasCondition<TFCond_UberBulletResist>(entity))
                cdmg = cdmg * 0.25f - 1.0f;
            else if (HasCondition<TFCond_SmallBulletResist>(entity))
                cdmg = cdmg * 0.90f - 1.0f;

            // Invis damage correction, Invis spies get protection from 10%
            // of damage

            // Check if player will die from headshot or if target has more
            // than 450 health and sniper has max charge
            float hsdmg = 150.0f;
            if (CE_INT(LOCAL_W, netvar.iItemDefinitionIndex) == 230)
                hsdmg = static_cast<int>(50.0f * 1.35f);

            auto health = static_cast<float>(entity->m_iHealth());
            if (!(health <= hsdmg || health <= cdmg || !g_pLocalPlayer->bZoomed || maxCharge && health > maxhs))
                return false;
        }
        // Vaccinator
        if (*ignore_vaccinator && IsPlayerResistantToCurrentWeapon(entity))
            return false;

        cd.hitbox = BestHitbox(entity);
        if (*vischeck_hitboxes && !*multipoint)
        {
            if (*vischeck_hitboxes == 1 && playerlist::AccessData(entity->player_info->friendsID).state != playerlist::k_EState::RAGE)
                return true;
            else
            {
                trace_t first_tracer;
                if (IsEntityVectorVisible(entity, entity->hitboxes.GetHitbox(cd.hitbox)->center, true, MASK_SHOT_HULL, &first_tracer, true))
                    return true;
                const uint8 max_box = entity->hitboxes.GetNumHitboxes();
                u_int8_t i          = 0;
                while (i < max_box) // Prevents returning empty at all costs. Loops through every hitbox
                {
                    if (i == cd.hitbox)
                    {
                        ++i;
                        continue;
                    }
                    trace_t test_trace;
                    Vector centered_hitbox = entity->hitboxes.GetHitbox(i)->center;

                    if (IsEntityVectorVisible(entity, centered_hitbox, true, MASK_SHOT_HULL, &test_trace, true))
                    {
                        cd.hitbox = i;
                        return true;
                    }
                    ++i;
                }
                return false; // It looped through every hitbox and found nothing. It isn't visible.
            }
        }
        return true;
    }
    // Check for buildings
    case (ENTITY_BUILDING):
    {
        // Enabled check
        if (!*buildings_sentry)
            return false;

        // Teammate's buildings can never be damaged
        if (!entity->m_bEnemy())
            return false;

        // Distance
        const float targeting_range = EffectiveTargetingRange();
        if (targeting_range > 0.0f && entity->m_flDistance() - 40.0f > targeting_range && tickcount > last_target_ignore_timer)
            return false;

        if (!*buildings_sentry && entity->m_iClassID() == CL_CLASS(CObjectSentrygun))
            return false;

        return true;
    }
    // NPCs (Skeletons, Merasmus, etc)
    case (ENTITY_NPC):
    {
        // NPC targeting is disabled
        if (!*npcs)
            return false;

        // Cannot shoot this
        if (!entity->m_bEnemy())
            return false;

        // Distance
        if (entity->m_flDistance() - 40.0f > EffectiveTargetingRange() && tickcount > last_target_ignore_timer)
            return false;

        return true;
    }
    default:
        break;
    }
    return false;
}

// A function to aim at a specific entity
bool Aim(CachedEntity *entity)
{
    // Get angles from eye to target
    Vector is_it_good = PredictEntity(entity);
    if (!IsEntityVectorVisible(entity, is_it_good, true, MASK_SHOT_HULL, nullptr, true))
        return false;

    Vector angles = GetAimAtAngles(g_pLocalPlayer->v_Eye, is_it_good, LOCAL_E);

    if (fov > 0 && cd.fov > fov)
        return false;
    // Slow aim
    if (slow_aim)
        DoSlowAim(angles);

#if ENABLE_VISUALS
    if (entity->m_Type() == ENTITY_PLAYER)
        hacks::esp::SetEntityColor(entity, colors::target);
#endif
    // Set angles
    current_user_cmd->viewangles = angles;

    if (*silent && !slow_aim)
        g_pLocalPlayer->bUseSilentAngles = true;
    // Set tick count to targets (backtrack messes with this)
    if (!ShouldBacktrack(entity) && *nolerp && entity->m_IDX <= g_GlobalVars->maxClients)
        current_user_cmd->tick_count = TIME_TO_TICKS(CE_FLOAT(entity, netvar.m_flSimulationTime));
    aimed_this_tick      = true;
    viewangles_this_tick = angles;
    // Finish function
    return true;
}

// A function to check whether player can autoshoot
bool began_charge = false;
void DoAutoshoot(CachedEntity *target_entity)
{
    // Enable check
    if (!*autoshoot)
        return;
    bool attack = true;

    // Rifle check
    if (g_pLocalPlayer->holding_sniper_rifle && *zoomed_only && !CanHeadshot() && !AllowNoScope(target_entity))
        attack = false;
    // Autoshoot breaks with Slow aimbot, so use a workaround to detect when it can
    else if (slow_aim != 0 && !slow_can_shoot)
        attack = false;
    // Don't autoshoot without anything in clip
    else if (CE_INT(LOCAL_W, netvar.m_iClip1) == 0)
        attack = false;

    if (attack)
    {
        // TO DO: Sending both reload and attack will activate the Hitman's Heatmaker ability
        // Don't activate it only on first kill (or somehow activate it before a shot)
        current_user_cmd->buttons |= IN_ATTACK | (*autoreload && CarryingHeatmaker() ? IN_RELOAD : 0);
        if (target_entity)
        {
            auto hitbox = cd.hitbox;
            hitrate::AimbotShot(target_entity->m_IDX, hitbox != head);
        }
        *bSendPackets = true;
    }
    if (LOCAL_W->m_iClassID() == CL_CLASS(CTFLaserPointer))
        current_user_cmd->buttons |= IN_ATTACK2;
}

// Grab a vector for a specific ent
Vector PredictEntity(CachedEntity *entity)
{
    // Pull out predicted data
    Vector &result            = cd.aim_position;
    const short int curr_type = entity->m_Type();

    // If using projectiles, predict a vector
    switch (curr_type)
    {
    // Player
    case ENTITY_PLAYER:
        {
            {
                // Allow multipoint logic to run
                if (!*multipoint)
                {
                    result = entity->hitboxes.GetHitbox(cd.hitbox)->center;
                    break;
                }

                std::optional<Vector> best_pos = GetBestHitpoint(entity, cd.hitbox);
                if (best_pos)
                    result = *best_pos;
            }
        }
        break;
    // NPCs (Skeletons, merasmus, etc)
    case ENTITY_NPC:
        result = entity->hitboxes.GetHitbox(std::max(0, entity->hitboxes.GetNumHitboxes() / 2 - 1))->center;
        break;
    // Other
    default:
        result = entity->m_vecOrigin();
        break;
    }
    cd.predict_tick = tickcount;
    cd.fov          = GetFov(g_pLocalPlayer->v_OrigViewangles, g_pLocalPlayer->v_Eye, result);

    // Return the found vector
    return result;
}

int NotVisibleHitbox(CachedEntity *target, int preferred)
{
    if (target->hitboxes.VisibilityCheck(preferred))
        return preferred;
    // Else attempt to find any hitbox at all
    else
        return hitbox_t::spine_1;
}

int AutoHitbox(CachedEntity *target)
{
    int preferred      = hitbox_t::spine_1;
    auto target_health = static_cast<float>(target->m_iHealth()); // This was used way too many times. Due to how pointers work (defrencing)+the compiler already dealing with tons of AIDS global variables it likely derefrenced it every time it was called.
    int ci             = LOCAL_W->m_iClassID();

    if (CanHeadshot()) // Nothing else zooms in this game you have to be holding a rifle for this to be true.
    {
        float cdmg = CE_FLOAT(LOCAL_W, netvar.flChargedDamage);
        float bdmg = 50.0f;
        // Vaccinator damage correction, protects against 20% of damage
        if (CarryingHeatmaker())
        {
            bdmg = bdmg * .80f - 1;
            cdmg = cdmg * .80f - 1;
        }
        // Vaccinator damage correction, protects against 75% of damage
        if (HasCondition<TFCond_UberBulletResist>(target))
        {
            bdmg = bdmg * .25f - 1;
            cdmg = cdmg * .25f - 1;
        }
        // Passive bullet resist protects against 10% of damage
        else if (HasCondition<TFCond_SmallBulletResist>(target))
        {
            bdmg = bdmg * .90f - 1;
            cdmg = cdmg * .90f - 1;
        }
        // Invis damage correction, Invis spies get protection from 10%
        // of damage
        else if (IsPlayerInvisible(target)) // You can't be invisible and under the effects of the vaccinator
        {
            bdmg = bdmg * .80f - 1;
            cdmg = cdmg * .80f - 1;
        }
        // If can headshot and if bodyshot kill from charge damage, or
        // if crit boosted, and they have 150 health, or if player isn't
        // zoomed, or if the enemy has less than 40, due to darwins, and
        // only if they have less than 150 health will it try to bodyshot
        if (std::floor(cdmg) >= target_health || IsPlayerCritBoosted(LOCAL_E) || target_health <= std::floor(bdmg) && target_health <= 150.0f)
        {
            // We don't need to hit the head as a bodyshot will kill
            preferred = hitbox_t::spine_1;
            return preferred;
        }

        return hitbox_t::head;
    }
    return preferred;
}

// Function to find the closest hitbox to the crosshair for a given ent
int ClosestHitbox(CachedEntity *target)
{
    // FIXME this will break multithreading if it will be ever implemented. When
    // implementing it, these should be made non-static
    int closest       = -1;
    float closest_fov = 256.0f, fov = 0.0f;

    for (int i = 0; i < target->hitboxes.GetNumHitboxes(); ++i)
    {
        fov = GetFov(g_pLocalPlayer->v_OrigViewangles, g_pLocalPlayer->v_Eye, target->hitboxes.GetHitbox(i)->center);
        if (fov < closest_fov || closest == -1)
        {
            closest     = i;
            closest_fov = fov;
        }
    }
    return closest;
}

// A helper function to find a user angle that isn't directly on the target
// angle, effectively slowing the aiming process
void DoSlowAim(Vector &input_angle)
{
    auto viewangles = current_user_cmd->viewangles;
    Vector slow_delta;

    // Don't bother if we're already on target
    if (viewangles != input_angle) [[likely]]
    {
        slow_delta = input_angle - viewangles;

        slow_delta.y = std::remainder(slow_delta.y, 360.0f);

        slow_delta /= static_cast<float>(slow_aim);
        input_angle = viewangles + slow_delta;

        // Clamp as we changed angles
        fClampAngle(input_angle);
    }

    // 0.17 is a good amount in general
    slow_can_shoot = std::abs(slow_delta.y) < 0.17f && std::abs(slow_delta.x) < 0.17f;
}

// A function that determins whether aimkey allows aiming
bool UpdateAimkey()
{
    static bool aimkey_flip       = false;
    static bool pressed_last_tick = false;
    bool allow_aimkey             = true;
    static bool last_allow_aimkey = true;

    // Check if aimkey is used
    if (aimkey && *aimkey_mode)
    {
        // Grab whether the aimkey is depressed
        bool key_down = aimkey.isKeyDown();
        switch (*aimkey_mode)
        {
        case 1: // Only while key is depressed
            if (!key_down)
                allow_aimkey = false;
            break;
        case 2: // Only while key is not depressed, enable
            if (key_down)
                allow_aimkey = false;
            break;
        case 3: // Aimkey acts like a toggle switch
            if (!pressed_last_tick && key_down)
                aimkey_flip = !aimkey_flip;
            if (!aimkey_flip)
                allow_aimkey = false;
            break;
        default:
            break;
        }
        pressed_last_tick = key_down;
    }
    // Return whether the aimkey allows aiming
    return allow_aimkey;
}

// Used mostly by navbot to not accidentally look at path when aiming
bool IsAiming()
{
    return aimed_this_tick;
}

// A function used by gui elements to determine the current target
CachedEntity *CurrentTarget()
{
    return target_last;
}

// Used for when you join and leave maps to reset aimbot vars
void Reset()
{
    target_last     = nullptr;
}

#if ENABLE_VISUALS
static void DrawText()
{
    // Don't draw to screen when aimbot is disabled
    if (!enable)
        return;

    // Fov ring to represent when a target will be shot
    if (*fov_draw)
    {
        // It can't use fovs greater than 180, so we check for that
        if (fov > 0.0f && fov < 180.0f)
        {
            // Don't show ring while player is dead
            if (CE_GOOD(LOCAL_E) && LOCAL_E->m_bAlivePlayer())
            {
                rgba_t color = colors::gui;
                color.a      = *fovcircle_opacity;

                int width, height;
                g_IEngine->GetScreenSize(width, height);

                // Math
                float mon_fov  = static_cast<float>(width) / static_cast<float>(height) / (4.0f / 3.0f);
                float fov_real = RAD2DEG(2.0f * atanf(mon_fov * tanf(DEG2RAD(draw::fov / 2.0f))));
                float radius   = tan(DEG2RAD(fov) / 2.0f) / tan(DEG2RAD(fov_real) / 2.0f) * static_cast<float>(width);

                draw::Circle(static_cast<float>(width) / 2, static_cast<float>(height) / 2, radius, color, 1, 100);
            }
        }
    }
    for (const auto &ent : entity_cache::player_cache)
    {
        Vector screen;
        Vector oscreen;
        if (draw::WorldToScreen(cd.aim_position, screen) && draw::WorldToScreen(ent->m_vecOrigin(), oscreen))
        {
            draw::Rectangle(screen.x - 2, screen.y - 2, 4, 4, colors::white);
            draw::Line(oscreen.x, oscreen.y, screen.x - oscreen.x, screen.y - oscreen.y, colors::EntityF(ent), 0.5f);
        }
    }
}
#endif

void RvarCallback(settings::VariableBase<float> &, float after)
{
    force_backtrack_aimbot = after >= 200.0f;
}

static InitRoutine EC(
    []()
    {
        hacks::backtrack::latency.installChangeCallback(RvarCallback);
        EC::Register(EC::LevelInit, Reset, "INIT_Aimbot", EC::average);
        EC::Register(EC::LevelShutdown, Reset, "SHUTDOWN_Aimbot", EC::average);
        EC::Register(EC::CreateMove, CreateMove, "CM_Aimbot", EC::late);
        EC::Register(EC::CreateMoveWarp, CreateMoveWarp, "CMW_Aimbot", EC::late);
    });

} // namespace hacks::aimbot
