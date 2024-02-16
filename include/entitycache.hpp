/*
 * entitycache.hpp
 *
 *  Created on: Nov 7, 2016
 *      Author: nullifiedcat
 */

#pragma once

#include "entityhitboxcache.hpp"
#include "averager.hpp"
#include <mathlib/vector.h>
#include <icliententity.h>
#include <icliententitylist.h>
#include <cdll_int.h>
#include <enums.hpp>
#include <core/interfaces.hpp>
#include <itemtypes.hpp>
#include "localplayer.hpp"
#include <core/netvars.hpp>
#include "playerresource.hpp"
#include "globals.hpp"
#include "classinfo/classinfo.hpp"
#include "client_class.h"
#include "Constants.hpp"
#include <optional>
#include <soundcache.hpp>

struct matrix3x4_t;

class IClientEntity;
struct player_info_s;
struct model_t;
struct mstudiohitboxset_t;
struct mstudiobbox_t;

#define PROXY_ENTITY true

#if PROXY_ENTITY
#define RAW_ENT(ce) ce->InternalEntity()
#else
#define RAW_ENT(ce) ce->m_pEntity
#endif

#define CE_VAR(entity, offset, type) NET_VAR(RAW_ENT(entity), offset, type)

#define CE_INT(entity, offset) CE_VAR(entity, offset, int)
#define CE_FLOAT(entity, offset) CE_VAR(entity, offset, float)
#define CE_BYTE(entity, offset) CE_VAR(entity, offset, unsigned char)
#define CE_VECTOR(entity, offset) CE_VAR(entity, offset, Vector)

#define CE_GOOD(entity) ((entity) && !g_Settings.bInvalid && (entity)->Good())
#define CE_BAD(entity) (!CE_GOOD(entity))
#define CE_VALID(entity) ((entity) && !g_Settings.bInvalid && (entity)->Valid())
#define CE_INVALID(entity) (!CE_VALID(entity))

#define IDX_GOOD(idx) ((idx) >= 0 && (idx) <= HIGHEST_ENTITY && (idx) < MAX_ENTITIES)
#define IDX_BAD(idx) !IDX_GOOD(idx)

#define HIGHEST_ENTITY (entity_cache::max)
#define ENTITY(idx) (entity_cache::Get(idx))

class CachedEntity
{
public:
    typedef CachedEntity ThisClass;
    CachedEntity();
    explicit CachedEntity(u_int16_t idx);
    ~CachedEntity();

    __attribute__((hot)) void Update();

    bool IsVisible();
    __attribute__((always_inline, hot, const)) IClientEntity *InternalEntity() const
    {
        return g_IEntityList->GetClientEntity(m_IDX);
    }

    __attribute__((always_inline, hot, const)) bool Good() const
    {
        if (!RAW_ENT(this) || !RAW_ENT(this)->GetClientClass()->m_ClassID)
            return false;
        IClientEntity *const entity = InternalEntity();
        return entity && !entity->IsDormant();
    }

    __attribute__((always_inline, hot, const)) bool Valid() const
    {
        if (!RAW_ENT(this) || !RAW_ENT(this)->GetClientClass()->m_ClassID)
            return false;
        IClientEntity *const entity = InternalEntity();
        return entity;
    }

    template <typename T> __attribute__((always_inline, hot, const)) T &var(uintptr_t offset) const
    {
        return *reinterpret_cast<T *>(uintptr_t(RAW_ENT(this)) + offset);
    }

    const u_int16_t m_IDX;

    int m_iClassID() const
    {
        if (this && RAW_ENT(this))
            if (RAW_ENT(this)->GetClientClass())
                if (RAW_ENT(this)->GetClientClass()->m_ClassID)
                    return RAW_ENT(this)->GetClientClass()->m_ClassID;
        return 0;
    };

    Vector m_vecOrigin() const
    {
        return RAW_ENT(this)->GetAbsOrigin();
    };

    std::optional<Vector> m_vecDormantOrigin() const
    {
        if (!RAW_ENT(this)->IsDormant())
            return m_vecOrigin();
        auto vec = soundcache::GetSoundLocation(this->m_IDX);
        if (vec)
            return *vec;
        return std::nullopt;
    }

    int m_iTeam() const
    {
        return NET_INT(RAW_ENT(this), netvar.iTeamNum);
    };

    bool m_bAlivePlayer() const
    {
        return !(NET_BYTE(RAW_ENT(this), netvar.iLifeState));
    };

    bool m_bEnemy() const
    {
        if (CE_BAD(g_pLocalPlayer->entity))
            return true;
        return m_iTeam() != g_pLocalPlayer->team;
    };

    int m_iMaxHealth()
    {
        switch (m_Type())
        {
        case ENTITY_PLAYER:
            return g_pPlayerResource->GetMaxHealth(this);
        case ENTITY_BUILDING:
            return NET_INT(RAW_ENT(this), netvar.iBuildingMaxHealth);
        default:
            return 0.0f;
        }
    };

    int m_iHealth() const
    {
        switch (m_Type())
        {
        case ENTITY_PLAYER:
            return NET_INT(RAW_ENT(this), netvar.iHealth);
        case ENTITY_BUILDING:
            return NET_INT(RAW_ENT(this), netvar.iBuildingHealth);
        default:
            return 0.0f;
        }
    };

    Vector &m_vecAngle() const
    {
        return CE_VECTOR(this, netvar.m_angEyeAngles);
    };

    // Entity fields start here
    EntityType m_Type() const
    {
        int classid = m_iClassID();
        switch (classid)
        {
        case CL_CLASS(CTFPlayer):
        {
            return ENTITY_PLAYER;
        }
        case CL_CLASS(CTFGrenadePipebombProjectile):
        case CL_CLASS(CTFProjectile_Cleaver):
        case CL_CLASS(CTFProjectile_Jar):
        case CL_CLASS(CTFProjectile_JarMilk):
        case CL_CLASS(CTFProjectile_Arrow):
        case CL_CLASS(CTFProjectile_EnergyBall):
        case CL_CLASS(CTFProjectile_EnergyRing):
        case CL_CLASS(CTFProjectile_GrapplingHook):
        case CL_CLASS(CTFProjectile_HealingBolt):
        case CL_CLASS(CTFProjectile_Rocket):
        case CL_CLASS(CTFProjectile_SentryRocket):
        case CL_CLASS(CTFProjectile_BallOfFire):
        case CL_CLASS(CTFProjectile_Flare):
        {
            return ENTITY_PROJECTILE;
        }
        case CL_CLASS(CObjectTeleporter):
        case CL_CLASS(CObjectSentrygun):
        case CL_CLASS(CObjectDispenser):
        {
            return ENTITY_BUILDING;
        }
        case CL_CLASS(CZombie):
        case CL_CLASS(CTFTankBoss):
        case CL_CLASS(CMerasmus):
        case CL_CLASS(CMerasmusDancer):
        case CL_CLASS(CEyeballBoss):
        case CL_CLASS(CHeadlessHatman):
        {
            return ENTITY_NPC;
        }
        default:
        {
            return ENTITY_GENERIC;
        }
        }
    };

    float m_flDistance() const
    {
        if (CE_GOOD(g_pLocalPlayer->entity))
            return g_pLocalPlayer->v_Origin.DistTo(m_vecOrigin());
        else
            return FLT_MAX;
    };

    bool m_bGrenadeProjectile() const
    {
        return m_iClassID() == CL_CLASS(CTFGrenadePipebombProjectile) || m_iClassID() == CL_CLASS(CTFProjectile_Cleaver) || m_iClassID() == CL_CLASS(CTFProjectile_Jar) || m_iClassID() == CL_CLASS(CTFProjectile_JarMilk);
    };

    static bool IsProjectileACrit(CachedEntity *ent)
    {
        if (ent->m_bGrenadeProjectile())
            return CE_BYTE(ent, netvar.Grenade_bCritical);
        return CE_BYTE(ent, netvar.Rocket_bCritical);
    }

    bool m_bCritProjectile()
    {
        switch (m_Type())
        {
        case EntityType::ENTITY_PROJECTILE:
            return IsProjectileACrit(this);
        default:
            return false;
        }
    };

    bool m_bAnyHitboxVisible{ false };
    bool m_bVisCheckComplete{ false };

    /*k_EItemType m_ItemType()
    {
        if (m_Type() == ENTITY_GENERIC)
            return g_ItemManager.GetItemType(this);
        else
            return ITEM_NONE;
    };*/

    unsigned long m_lLastSeen{ 0 };
    Vector m_vecVelocity{ 0 };
    Vector m_vecAcceleration{ 0 };
    hitbox_cache::EntityHitboxCache hitboxes;
    player_info_s *player_info = nullptr;
    void Reset()
    {
        m_bAnyHitboxVisible = false;
        m_bVisCheckComplete = false;
        m_lLastSeen         = 0;
        if (player_info)
            memset(player_info, 0, sizeof(player_info_s));
        m_vecAcceleration.Zero();
        m_vecVelocity.Zero();
    }

    bool was_dormant() const
    {
        return RAW_ENT(this)->IsDormant();
    };

    bool velocity_is_valid{ false };
#if !PROXY_ENTITY
    IClientEntity *m_pEntity{ nullptr };
#endif
};

namespace entity_cache
{  
extern u_int16_t max;
extern u_int16_t previous_max;
extern std::vector<CachedEntity *> valid_ents;
extern std::unordered_map<u_int16_t, CachedEntity> array;
extern std::vector<CachedEntity *> player_cache;
inline CachedEntity *Get(const u_int16_t &idx)
{
    auto test = array.find(idx);
    if (test == array.end())
        return nullptr;
    else
        return &test->second;
}
__attribute__((hot)) void Update();
void Invalidate();
void Shutdown();
} // namespace entity_cache