/*
 * enums.hpp
 *
 *  Created on: Oct 7, 2016
 *      Author: nullifiedcat
 */

#pragma once

enum EntityType
{
    ENTITY_GENERIC,
    ENTITY_PLAYER,
    ENTITY_BUILDING,
    ENTITY_PROJECTILE,
    ENTITY_NPC
};

enum powerup_type
{
    not_powerup = -1,
    strength,
    resistance,
    vampire,
    reflect,
    haste,
    regeneration,
    precision,
    agility,
    knockout,
    king,
    plague,
    supernova,
    crits,
    POWERUP_COUNT
};

enum powerup_owner
{
    neutral = 0,
    red     = 1,
    blue    = 2
};

enum tf_team
{
    TEAM_UNK = 0,
    TEAM_SPEC,
    TEAM_RED,
    TEAM_BLU
};

enum tf_class
{
    tf_scout = 1,
    tf_sniper,
    tf_soldier,
    tf_demoman,
    tf_medic,
    tf_heavy,
    tf_pyro,
    tf_spy,
    tf_engineer
};

enum MinigunState_t
{
    AC_STATE_IDLE = 0,
    AC_STATE_STARTFIRING,
    AC_STATE_FIRING,
    AC_STATE_SPINNING
};

enum weaponmode
{
    weapon_invalid = -1,
    weapon_hitscan = 0,
    weapon_projectile,
    weapon_melee,
    weapon_pda,
    weapon_medigun,
    weapon_consumable,
    weapon_throwable
};

enum hitbox_t
{
    head       = 0,
    pelvis     = 1,
    spine_0    = 2,
    spine_1    = 3,
    spine_2    = 4,
    spine_3    = 5,
    upperArm_L = 6,
    lowerArm_L = 7,
    hand_L     = 8,
    upperArm_R = 9,
    lowerArm_R = 10,
    hand_R     = 11,
    hip_L      = 12,
    knee_L     = 13,
    foot_L     = 14,
    hip_R      = 15,
    knee_R     = 16,
    foot_R     = 17,

    // Always last
    HITBOXES_SIZE
};

// Spectator Movement modes
enum
{
    OBS_MODE_NONE = 0,  // not in spectator mode
    OBS_MODE_DEATHCAM,  // special mode for death cam animation
    OBS_MODE_FREEZECAM, // zooms to a target, and freeze-frames on them
    OBS_MODE_FIXED,     // view from a fixed camera position
    OBS_MODE_IN_EYE,    // follow a player in first person view
    OBS_MODE_CHASE,     // follow a player in third person view
    OBS_MODE_POI,       // PASSTIME point of interest - game objective, big fight, anything interesting; added in the middle of the enum due to tons of hard-coded "<ROAMING" enum compares
    OBS_MODE_ROAMING,   // free roaming

    NUM_OBSERVER_MODES
};
